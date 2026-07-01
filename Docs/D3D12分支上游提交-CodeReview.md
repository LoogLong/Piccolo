# 分支 Code Review 报告：`piccolo-render-d3d12-增强`

> 目的：为将本分支提交到中央（上游）Piccolo 仓库做准备。本文档罗列当前分支相较上游基线存在的问题，
> 并按“高内聚、低耦合、便于上游 review”的目标给出拆分与修复建议。
>
> - 上游基线提交：`f7a2751`（`Fix JoltPhysics compile error ...`）
> - 被审查分支 HEAD：`0204f8e`
> - 差异规模：约 **150 个文件、+22910 / -1092 行**
> - 审查方式：`git diff f7a2751 HEAD` 全量差异 + 关键文件精读

---

## 0. 总体结论（TL;DR）

1. **必须先拆分 PR。** 本分支把两件基本独立的大事混在一起：
   - **A. D3D12 渲染后端 + DXR 路径追踪 + GPU Skinning**（渲染方向）；
   - **B. `lganim` 运动匹配（Motion Matching / LMM）动画系统 + 72MB 二进制资源**（玩法/动画方向）。

   两者除了“skinning 结果被渲染消费”“坐标系转换/四元数工具函数”外没有编译期依赖。混在一个 PR 会让上游 reviewer 无法独立合入渲染后端，且被迫接收 72MB 资源与一整套改变默认角色控制逻辑的代码。**建议至少拆成 2 个 PR。**

2. **必须清理仓库卫生问题。** 当前提交了不该进版本库的文件：`imgui.ini`（用户级窗口布局）、开发者本机化的 `PiccoloEditor.ini`、以及 4 个二进制资源（`database.bin` 64MB，合计 ~72MB）。

3. **RHI 抽象存在“后端泄漏”。** `getBackendType() == D3D12` 这类判断散布在 pass / pipeline / resource 层；`rhi_ray_tracing.h` 把 D3D12/DXIL 概念（`wchar_t*` export 名）写进了通用 RHI 头；Vulkan 光追接口是空实现，两后端不对称。

4. **D3D12 后端本身可用但工程化不足。** `d3d12_rhi.cpp` 单文件 **10520 行**；存在死代码、错误处理不一致（记录 → 静默返回）、契约被破坏（`createBufferWithAllocation` 不填 `pAllocation`）、光追管线/SBT 无销毁路径等。

5. **存在若干确认的代码 bug**（见 §5），其中至少一处是“失败当成功处理”。

成熟度速评：D3D12 光栅 ≈ Beta；DXR/路径追踪 ≈ 实验性 POC；RHI 双后端抽象 ≈ Alpha；`lganim` ≈ 原型质量。

---

## 1. 提交范围与拆分建议（最高优先级）

### 1.1 建议的 PR 拆分

| PR | 内容 | 说明 |
| --- | --- | --- |
| **PR-1 渲染后端** | `interface/rhi*.h`、`interface/d3d12/**`、`interface/vulkan/**` 改动、`passes/**`、`render_pipeline*`、`render_resource*`、`render_scene*`、`render_system*`、HLSL shader、`cmake/ShaderCompile.cmake`、CMake 后端选项、`.github/workflows/build_windows.yml`、渲染相关数学（`makeOrthographicProjectionMatrix01`） | 让上游可以只 review/合入渲染后端 |
| **PR-2 动画系统** | `function/animation/lganim/**`、`animation_component.*`、`motor_component.cpp`、`level_debugger.*`、`world_manager.h` / `engine.cpp` 帧计数改动、动画相关数学（四元数/`RightHandYUpToZUp`/`Transform::operator*`）、资源加载管线 | 需先补 license、跨平台 I/O、去二进制 blob |
| **（不入库）** | 72MB `*.bin`、`imgui.ini`、本机化 dev 配置 | 见 §2 |

### 1.2 `lganim` 不应进入 D3D12 PR 的理由
- 模块自包含在 `engine/source/runtime/function/animation/lganim/`（11 个文件、~2200 行），**内部无任何 D3D12/Vulkan/渲染 API 调用**（仅 `anim_instance.cpp` 有一个未使用的 `#include ".../render_type.h"`）。
- 但它通过集成点**改变了全局默认玩法**：`animation_component.cpp:16-17` 把所有角色硬切到 `CAnimInstanceMotionMatching`；`motor_component.cpp:87-94` 在 `HasRootMotion()` 时改走 root-motion 分支，而 `HasRootMotion()` 恒为 `true`（`mm_instance.cpp:595-597`）。
- 它**硬依赖** 4 个二进制资源（见 §2.3、§6.3），缺失会在构造函数 `assert` 失败。

---

## 2. 仓库卫生问题（不该入库/生成物）

| 文件 | 问题 | 建议 |
| --- | --- | --- |
| `imgui.ini`（仓库根） | 用户级 ImGui 窗口/停靠布局，含 `Size=2560,1369` 等本机分辨率状态（`imgui.ini:26-27`），因人而异 | 从版本库移除，加入 `.gitignore` |
| `engine/configs/development/PiccoloEditor.ini` | 开发者本机化：`RenderBackend=D3D12`、`RenderSceneMode=PathTracing`、`RenderBackendAllowFallback=true`、相对 `BinaryRootFolder=../../../../../bin`（`:1,10-12`） | dev 配置的后端/场景模式默认值属“产品策略”，需团队确认；本机路径不应提交 |
| `engine/configs/deployment/PiccoloEditor.ini` | 同样把默认后端/场景模式改为 D3D12 + PathTracing | 作为默认策略需显式评审，勿随动画改动夹带 |
| `engine/asset/animation/database.bin` | **64 MB** | 不应作为普通 git blob；用 Git LFS / 下载脚本 / 构建期烘焙 |
| `engine/asset/animation/my_latent.bin` | 6.8 MB，且 `my_*` 命名不规范 | 同上；改用有意义命名 |
| `engine/asset/animation/my_decompressor.bin` | 819 KB | 同上 |
| `engine/asset/animation/character.bin` | 340 KB | 同上 |

补充：
- `engine/asset/.gitignore` 仅有 `!*.obj` 一条白名单，并未排除 `*.bin`，因此这些二进制被正常纳入。建议新增忽略规则（如 `engine/asset/animation/*.bin`）。
- 根 `.gitignore` 已新增 `build_d3d12_only/`、`build_dual_backend/`（合理），但**未忽略 `imgui.ini`**。
- 4 个 `*.bin` 合计约 **72 MB**，一旦进入 git 历史将永久增大仓库体积，上游极可能拒绝。

---

## 3. RHI 抽象与耦合问题（高内聚 / 低耦合核心）

### 3.1 后端类型判断泄漏到引擎层
`getBackendType()` / `RHIBackendType::` 在**非 RHI 实现层**出现约 40 处，其中 `passes + pipeline + resource` 层就有 **16 处**：

| 位置 | 用途 | 问题 |
| --- | --- | --- |
| `render_pipeline.cpp:~199-208, ~258-267` | D3D12 在 `submitRendering` 前做 `copyNormalAndDepthImage`，Vulkan 在其后 | 管线编排层被迫感知各后端的队列/提交时序 |
| `path_tracing_pass.cpp:~39-42, ~78-79` | 仅 D3D12 + DXR 才启用 | 特性开关用后端枚举而非能力查询 |
| `gpu_skinning_pass.cpp:~27-28` | 纯 compute 却要求 D3D12 + DXR | 纯计算特性被绑定到光追后端 |
| `render_resource.cpp:~24, ~144` | 投影矩阵手性、AS 输入 usage flag 按后端分支 | 资源层泄漏后端差异 |
| `ui_pass.cpp` | `switch(getBackendType())` + `#if PICCOLO_ENABLE_*` + 直接 `static_cast<VulkanRHI/D3D12RHI>`（~14 处） | ImGui 集成耦合面最大 |

**建议**：以 RHI **能力查询**（如 `supportsRayTracing()`、`supportsPathTracing()`、`requiresPreSubmitDepthCopy()`）或 capability struct 取代散落的 `if (backend == D3D12)`；把 ImGui 初始化收敛为中立的 `RHIUiBackendInitInfo`。

### 3.2 通用 RHI 头泄漏 D3D12/DXIL 概念
- `rhi_ray_tracing.h:32-35` 定义了 `static constexpr const wchar_t* kPathTracingRayGenExport = L"PathTracingRayGen"` 等——`wchar_t*` export 名是 D3D12/DXIL 约定，Vulkan 用 `const char* pName`；且这些是**路径追踪 pass 专属**的常量，不该放在通用 RHI 头。
- `rhi_ray_tracing.h:73-80` `RHIRayTracingShaderLibrary` 内嵌宽字符 export 字段，把所有后端绑到 D3D12 模型。
- **建议**：把这些常量移到路径追踪 pass / shader 元数据；RHI 层使用中立的 `const char*` 或后端各自映射。

### 3.3 两后端不对称：Vulkan 光追为空实现
`vulkan_rhi.cpp:~2517-2560` 中 `getRayTracingCapabilities()` 返回 `{}`，`cmdTraceRays()` 为空 no-op；而 D3D12 有完整 DXR 路径（`d3d12_rhi.cpp:~7462-7969`）。RHI “声明支持光追”但只有一个后端实现。
- **建议**：要么实现 Vulkan 光追，要么在接口/文档中显式标注 “ray tracing = 实验性、当前仅 D3D12”，并让 Vulkan 走能力查询返回“不支持”，避免上层误用。

### 3.4 公共头暴露后端句柄
`d3d12_rhi.h:31-39` 暴露 `getD3D12Device()` / `getD3D12CommandQueue()` / `getD3D12SwapchainFormat()` 等，`ui_pass.cpp:~78-91` 直接 `static_cast<D3D12RHI>` 调用。Vulkan 侧同样公开 `m_device`/`m_instance`。这是既有 Piccolo 风格，但对上游而言两后端都破坏了封装。

---

## 4. D3D12 后端代码质量

### 4.1 单文件过大（可维护性）
`d3d12_rhi.cpp` **10520 行**：匿名命名空间包装结构、格式/屏障工具、描述符/根签名、swapchain/帧循环、光追、PSO 创建全部堆在一个文件。
- **建议**：按职责拆分为 `d3d12_resource`、`d3d12_format_barrier`、`d3d12_descriptor`、`d3d12_swapchain`、`d3d12_raytracing`、`d3d12_pipeline` 等编译单元。

### 4.2 死代码 / 未接线
- `d3d12_descriptor_heap.{h,cpp}` 中的 `D3D12DescriptorHeapAllocator` **从未被 `d3d12_rhi.cpp` 使用**；实际描述符管理用内联的 `m_d3d12_*_descriptor_next` 计数器手动完成。要么接线要么删除。

### 4.3 错误处理不一致
- 初始化路径大量 `throw std::runtime_error`（约 32 处）；
- 但**命令录制路径**常“记录 `LOG_WARN` 后静默 return”（如 `cmdBindDescriptorSetsPFN`、`cmdDraw ~7323`、`cmdTraceRays ~7913-7918`）。上层按 Vulkan 语义假定命令已录制，D3D12 可能静默产出空/错帧而不报错。
- **建议**：录制路径失败至少加 debug assert，或统一错误上报。

### 4.4 破坏 API 契约：`createBufferWithAllocation`
`d3d12_rhi.cpp:~4490-4516` 直接 `pAllocation = nullptr`；而 Vulkan 侧用 VMA 填充 `VulkanAllocation`（`vulkan_rhi.cpp:~2923-2940`）。`freeAllocation` 又按裸指针 delete。依赖 allocation 追踪的调用方在 D3D12 上会失效。
- **建议**：D3D12 侧要么实现 allocation，要么在接口文档明确不支持并在 debug 下断言。

### 4.5 光追资源销毁缺失（泄漏风险）
- RHI 无 `destroyRayTracingPipeline`；`PathTracingPass` 持有 `m_ray_tracing_pipeline` / `m_shader_binding_table`（`path_tracing_pass.h:~91-92`）却无析构清理，GPU 对象仅靠进程退出释放。
- `cmdTraceRays` 内 `~7931-7934` 在 dispatch 期间就地改写共享 pipeline 的 `layout`，pipeline 复用时脆弱。

### 4.6 魔法数字 / 硬编码上限
| 位置 | 值 | 说明 |
| --- | --- | --- |
| `d3d12_rhi.cpp:~3718` | CBV/SRV/UAV heap 65536 | 无配置，溢出即绑定失败 |
| `~3738` | Sampler heap 2048 | 同上 |
| `~9763/~9778` | RTV/DSV 容量 1024 | 同上 |
| `~7768-7769` | RT payload 32 / attributes 8 | 不可配置 |
| `~8587-8597` | `getPhysicalDeviceProperties` 返回硬编码 limits（`maxStorageBufferRange=128MB` 等） | 误报设备能力，影响共享代码的尺寸/对齐决策 |
| `~3734` | ImGui 占用描述符 slot 0，引擎分配从 1 起 | 隐式耦合，易被破坏 |

### 4.7 进程级静态注册表
`d3d12_rhi.cpp:~1195` `trackedHostVisibleDefaultBuffers()` 用函数内 `static std::vector<D3D12RHIBuffer*>` 追踪 buffer 指针；若销毁时未调用 `unregisterHostVisibleDefaultBuffer` 就会留下悬垂指针。上游高风险模式。

### 4.8 DXR 能力靠 SDK 宏静默降级
`d3d12_rhi.cpp:20-24` 用 `#ifdef D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT` 决定 `PICCOLO_D3D12_HAS_DXR`；旧 SDK 下光追“静默消失”，无运行时提示。

---

## 5. 渲染管线 / Pass 层

### 5.1 确认的 Bug：失败当成功处理
`particle_pass.cpp:1298-1300`：第二个描述符集分配失败时打印 `LOG_INFO("allocate normal and depth descriptor set done")`，而不是像上面（`:1293`）一样 `throw`。**失败被记成“完成”**，属真实 bug。
```cpp
if (RHI_SUCCESS != m_rhi->allocateDescriptorSets(&particle_descriptor_set_alloc_info,
                                                 m_descriptor_infos[eid * 3 + 1].descriptor_set))
    LOG_INFO("allocate normal and depth descriptor set done");   // 应为 throw
```
另：`particle_pass.cpp:~992` 成功路径抛出文案为 `"setup particle compute Descriptor done"` 的 `runtime_error`，错误文案误导。

### 5.2 陈旧/错误日志
`render_scene.cpp:~242-244` 记录“跳过 skinned/vertex-blended 实例”，但 `skipped_skinned` 变量声明后从未自增（`~175-177`），且 skinned mesh 现已被纳入（`~216-225`）。日志与实际逻辑不符。

### 5.3 路径追踪 composite pass 重复
`main_camera_pass.cpp:~543-680` `setupPathTracingCompositeRenderPass()` 几乎复制了主渲染 pass 的 subpass 图；`~2329-2385` `drawPathTracing()` 用硬编码 `cmdNextSubpass` 次数跳过 subpass 0-1、3-5。主 pass 任何 subpass 调整都要手动同步到此处，易漂移。

### 5.4 资源清理为空
`render_resource.cpp:~41-43` `RenderResource::clear()` 为空实现——PT 缓冲、BLAS、skinned 输出在关闭/重载时不释放，存在泄漏。

### 5.5 死代码 / 未用字段（新 PT pass）
- `path_tracing_pass.cpp:~778-781` 计算的 `write_count` / `p_writes` 未使用。
- `m_descriptor_set_dirty`、`m_accumulation_recreated_this_frame`、`m_last_blas_build_count`、`m_last_tlas_rebuilt`（`path_tracing_pass.h/.cpp`）只写不读（疑似遗留调试遥测）。
- `path_tracing_pass.cpp:~953-954` `static_data_changed` 中 `m_last_collected_instance_count == 0` 分支在空列表提前返回后不可达。

### 5.6 调试日志残留
`particle_pass.cpp:~994, ~1088, ~1300` 有无条件 `LOG_INFO`（每 emitter / 每 init 触发一次），应移除或加条件。

### 5.7 shader 字节码头（澄清，非 blob）
`render_shader_bytecode.h`（511 行）是**胶水头**，不是二进制 blob：通过 `__has_include` 在运行时选择 SPIR-V（`cpp/*.h`）或 DXIL（`dxil_cpp/*.h`）的构建期生成头，统一宏为 `PICCOLO_RENDER_SHADER_BYTECODE(rhi, NAME)`。各 Pass 的小改动（每处 ~14 行）就是把 `createShaderModule(X)` 机械替换为 `createShaderModule(PICCOLO_RENDER_SHADER_BYTECODE(m_rhi, X))`，**这部分改造干净、可上游**。
- 注意：`render_shader_bytecode.h` 本身应提交；但**构建生成的** `cpp/`、`dxil/`、`dxil_cpp/` 不应入库，需靠 CI shader 编译产出，并在文档说明 D3D12 依赖 `dxc.exe`。

### 5.8 其它正确性/性能隐患
- 非 DXR/Vulkan 环境下配置仍是 `RenderSceneMode=PathTracing`，会**静默回退为光栅且无提示**（UX 陷阱）。
- `drawPathTracing()` 跳过 tone mapping / color grading / FXAA（`~2369-2374`），HDR/调色链被绕过——若为有意需文档说明。
- 每个脏帧全量重建 TLAS（`~978-985`）；静态网格脏时 destroy+recreate BLAS（`render_resource.cpp:~226-229`）——性能可优化。
- `GpuSkinningPass` 逐实例重写全部描述符 + 重绑管线（`~377-385`），多 skinned 实例场景性能堪忧。

---

## 6. `lganim` 动画模块

### 6.1 模块定位
这是一套自包含的 **Motion Matching + Learned Motion Matching（LMM）** 运动匹配原型：`database.h`（姿态数据库/特征归一化/层次包围盒搜索）、`spring.h`（弹簧阻尼 + 惯性化混合）、`neural_network.h`（前馈网络 + `LoadNetwork`/`LoadLatent`）、`mm_instance.*`（完整运行时：输入→轨迹→搜索→惯性化→重定向→root motion）。

### 6.2 缺少 license / attribution（阻断项）
所有 `lganim` 文件**无任何版权、license 或来源 URL**。`spring.h`、`database.h` 搜索/惯性化、`array.h` 的 `Slice1D/Array2D` 与公开的 Daniel Holden Motion Matching / LMM 参考实现高度相似（通常 MIT）。上游合入前必须补充明确出处与 license 兼容性说明。

### 6.3 平台可移植性（阻断非 Windows）
- 二进制 I/O 全部用 **`fopen_s` + `FILE*`**（`database.h:48`、`neural_network.h:85/109`、`mm_instance.cpp:32`）——MSVC 专属，Linux/macOS 无法编译。
- `array.h` 用裸 `malloc/free/realloc`，与引擎 `std::vector` 风格不一致，对非 POD `T` 无构造/析构。
- 4 处硬编码资源路径 `asset/animation/*.bin`（`mm_instance.cpp:54-55, 117-121, 154-156`）。

### 6.4 硬编码/角色特定逻辑
- `enum Bones` 固定 23 根骨骼名（`mm_instance.h:9-35`），绑定单一 mocap 骨架。
- `m_retarget_map` 58 项手工映射（`mm_instance.cpp:125-152`）绑定单一目标骨架。
- `camera_yaw = Math_PI` 硬编码（`mm_instance.cpp:267`）。

### 6.5 确认的疑似 Bug：速度参数写反
`mm_instance.cpp:263-265` 中 `back` 速度用了 `run_side/walk_side`，`side` 速度用了 `run_back/walk_back`，疑似前后向/侧向参数交换：
```cpp
float simulation_back_speed = Math::lerp(m_simulation_run_side_speed, m_simulation_walk_side_speed, m_desired_gait);
float simulation_side_speed = Math::lerp(m_simulation_run_back_speed, m_simulation_walk_back_speed, m_desired_gait);
```

### 6.6 死代码 / 设计异味
- `#define LMM 0` + `#if LMM`（`mm_instance.cpp:380-381` 等，约 120 行）常驻但被禁用；即便禁用，神经网络资产仍在构造函数无条件加载（`:117-121`）。
- `HasRootMotion()` 恒返回 `true`（`:595-597`），导致所有 MM 角色永远走 root-motion，禁用了常规位移。
- `BuildMatchingFeature` 在每次构造时运行（`:56`），O(帧数 × 骨骼) 的工作本应预烘焙进 `database.bin`。
- 遗留大段注释调试块含 `printf`（`~1189-1213`）；变量 `m_crrent_time` 拼写错误（`mm_instance.h:100`）；`m_lmm_enable` 声明但未使用。

### 6.7 集成脆弱性
- `level_debugger.cpp:275` 对 `CAnimInstanceMotionMatching*` 做 `dynamic_cast` **无空指针检查**，若重新启用 `CSingleAnimInstance` 会崩溃。
- `animation_component.cpp:16-17` 注释掉旧路径、全局强制 MM，非数据驱动。
- 生产路径存在中文注释、Tab 与空格混用，与引擎其余风格不一致。

---

## 7. 数学库改动

大多为**附加式、向后兼容**（新方法/重载）：`Quaternion::abs()`、`toAngleAxis()`、`QuaternionFromXFormXY/Cols`、标量运算符、`Transform::operator*`、`Math::RightHandYUpToZUp` 等，主要服务 `lganim`；`makeOrthographicProjectionMatrix01` 服务渲染。

风险点：
- `quaternion.cpp:~175-188` `getQuaternionBetweenDirection` 被**重写**（新增提前返回 + 不同构造路径），属对既有行为的改动，需回归验证既有调用方。
- `math.h:9` 新增 `CMP` 宏但除定义外未使用，可删。
- 归属建议：动画相关数学随 PR-2，`makeOrthographicProjectionMatrix01` 随 PR-1。

---

## 8. 构建系统与 CI

总体质量较好，适合随渲染 PR 上游：
- `PICCOLO_ENABLE_VULKAN_BACKEND` / `PICCOLO_ENABLE_D3D12_BACKEND` 带平台守卫（`engine/CMakeLists.txt:10-27`）。
- DXC 探测搜索 Vulkan SDK / 捆绑路径 / `%VULKAN_SDK%` / Windows SDK，非单一开发者硬路径（`engine/CMakeLists.txt:73-112`）。
- `.github/workflows/build_windows.yml` 有 `d3d12_only` 与 `dual_backend` 矩阵，并在 Debug 下跑 `smoke_backend_boot.ps1 -RenderBackend D3D12`。

待清理项：
- `build_windows.bat:1-6`：`//cmake` 非法批处理注释；`del build`/`del bin` 缺 `/Q`；CI 未使用。
- `engine/CMakeLists.txt:111`：缺 dxc 时 `FATAL_ERROR`，对无 SDK 的开发者偏严（对 D3D12 构建是合理的）。
- `engine/shader/CMakeLists.txt:~74-100`：大段注释掉的旧 shader 嵌入逻辑，属噪音。
- `engine/source/runtime/CMakeLists.txt:~45-47`：资产拷贝到构建目录被注释掉，依赖 editor 后置拷贝。
- CI 仅覆盖渲染，不验证 lganim / 二进制资源。

---

## 9. 修复优先级清单

### 阻断项（合并前必须处理）
1. **拆分 PR**：渲染后端 与 lganim 动画分开（§1）。
2. **移除不该入库文件**：`imgui.ini`、本机化 dev `PiccoloEditor.ini`、4 个 `*.bin`（改 LFS/下载/烘焙），并补 `.gitignore`（§2）。
3. **修复 `particle_pass.cpp:1298-1300`**：分配失败必须 `throw`，不能记成功（§5.1）。
4. **`lganim` 上游前**：补 license/attribution、`fopen_s→std::fstream`/引擎资产加载、去二进制 blob、修速度写反 bug、`HasRootMotion` 数据驱动、`level_debugger` 空指针保护（§6）。
5. **明确 DXR/路径追踪定位**：作为独立 PR 或用能力开关门控，避免 Vulkan 空实现造成的“伪双后端”（§3.3）。

### 高优先（强烈建议）
6. 以 RHI 能力查询替换散落的 `getBackendType() == D3D12`（§3.1）。
7. 把 `rhi_ray_tracing.h` 中的 `wchar_t*` export 名移出通用 RHI 头（§3.2）。
8. 修复 `createBufferWithAllocation` 的 allocation 契约（§4.4）。
9. 补光追 pipeline/SBT 销毁路径；停止在 `cmdTraceRays` 内改写共享 pipeline（§4.5）。
10. 实现 `RenderResource::clear()` 释放 PT/BLAS/skinned 资源（§5.4）。
11. 修正 `render_scene.cpp` 陈旧日志（§5.2）。
12. 处理非 DXR 环境下 `RenderSceneMode=PathTracing` 静默回退——启动时给出可见告警（§5.8）。

### 中优先（工程化 / 清理）
13. 拆分 `d3d12_rhi.cpp`（10520 行）为多个编译单元（§4.1）。
14. 删除或接线 `D3D12DescriptorHeapAllocator`（§4.2）。
15. 命令录制失败路径至少加 debug assert（§4.3）。
16. 清理调试日志、死代码、未用字段（§5.5、§5.6、§6.6）。
17. 清理 `build_windows.bat`、注释掉的 shader/CMake 逻辑（§8）。
18. 魔法数字改为可配置或具名常量（§4.6）。

---

## 附：审查中确认的量化数据

| 指标 | 数值 |
| --- | --- |
| 差异规模 | ~150 文件，+22910 / -1092 |
| `d3d12_rhi.cpp` 行数 | 10520 |
| `mm_instance.cpp` 行数 | 1325 |
| 提交的二进制资源 | 4 个，合计 ~72 MB（`database.bin` 64 MB） |
| `passes+pipeline+resource` 中 `getBackendType()` | 16 处 |
| D3D12 侧 `FAILED(`/`SUCCEEDED(` | ~86 |
| D3D12 侧 `throw std::runtime_error` | ~32 |
| D3D12 侧裸 `new`/`delete` | ~30 / ~91 |
| Vulkan 光追接口 | 空实现（no-op） |
| `lganim` 中 license/attribution | 0 |
| `lganim` 中 `fopen_s` 调用点 | 4 |
