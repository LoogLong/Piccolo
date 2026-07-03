#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#include <unordered_map>

#include "runtime/core/base/macro.h"
#include "runtime/engine.h"
#include "runtime/function/global/global_context.h"

#include "editor/include/editor.h"

// https://gcc.gnu.org/onlinedocs/cpp/Stringizing.html
#define PICCOLO_XSTR(s) PICCOLO_STR(s)
#define PICCOLO_STR(s) #s

int main(int argc, char** argv)
{
    using namespace Piccolo;

    std::filesystem::path executable_path(argv[0]);
    std::filesystem::path config_file_path = executable_path.parent_path() / "PiccoloEditor.ini";

    PiccoloEngine* engine = new PiccoloEngine();

    engine->startEngine(config_file_path.generic_string());

    LOG_INFO("PiccoloEditor executable: {}",
             std::filesystem::absolute(executable_path).generic_string());
    LOG_INFO("PiccoloEditor config: {}", std::filesystem::absolute(config_file_path).generic_string());
    LOG_INFO("PiccoloEditor working directory: {}",
             std::filesystem::absolute(std::filesystem::current_path()).generic_string());

    engine->initialize();

    PiccoloEditor* editor = new PiccoloEditor();
    editor->initialize(engine);

    editor->run();

    editor->clear();

    engine->clear();
    engine->shutdownEngine();

    return 0;
}
