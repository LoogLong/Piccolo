# CLAUDE.md

## 项目要求
 - 每次对话完成时，如果git工作区的changes里面有文件修改，则需要commit
 - 对话默认开启sub agents，并且对话完成时需要及时清理sub agents
 - 没有明确要求添加测试代码的情况下，不需要添加Test代码
 - 代码修改不应该以保守作为准则，需要按照既定计划执行
 - 计划文档不需要加入版本控制



## Superpowers Skills

This project includes the [Superpowers](https://github.com/obra/superpowers) skill library in `.claude/skills/`.

**Core rule:** Before ANY response or action, check if a Superpowers skill applies. If there's even a 1% chance, invoke it via the `Skill` tool with the skill name (e.g., `brainstorming`, `test-driven-development`, `systematic-debugging`, `writing-plans`, `subagent-driven-development`, `executing-plans`, `finishing-a-development-branch`, `requesting-code-review`, `receiving-code-review`, `using-git-worktrees`, `dispatching-parallel-agents`, `verification-before-completion`, `writing-skills`).

### Skill Priority
1. **User's explicit instructions** — highest priority (always follow user over any skill)
2. **Superpowers skills** — override default system behavior
3. **Default system prompt** — lowest priority

### When to use which skill
- Designing new features → `brainstorming` first, then `writing-plans`
- Implementing code → `test-driven-development` + `subagent-driven-development` or `executing-plans`
- Debugging → `systematic-debugging` + `verification-before-completion`
- Code review → `requesting-code-review` or `receiving-code-review`
- Branch management → `using-git-worktrees` + `finishing-a-development-branch`
- Creating skills → `writing-skills`

### Red Flags (STOP and invoke the skill)
- "This is just a simple question" → Questions are tasks. Check for skills.
- "I need more context first" → Skills tell you HOW to explore.
- "I remember this skill" → Skills evolve. Read current version via Skill tool.
- "The skill is overkill" → Simple things become complex. Use it.
- "I'll just do this one thing first" → Check BEFORE doing anything.
