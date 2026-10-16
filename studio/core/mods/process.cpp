#include "core/mods/process.h"

#include "core/files/paths.h"

#include <SDL3/SDL.h>

namespace studio {

Job::Job(std::vector<std::string> args, fs::path working_directory)
    : args_(std::move(args)), working_directory_(std::move(working_directory)) {
    thread_ = std::thread([this] { run(); });
}

Job::~Job() {
    stop();
    if (thread_.joinable())
        thread_.join();
}

std::string Job::error() const {
    std::lock_guard lock(mutex_);
    return error_;
}

std::vector<std::string> Job::lines() const {
    std::lock_guard lock(mutex_);
    return lines_;
}

std::string Job::last_line() const {
    std::lock_guard lock(mutex_);
    return lines_.empty() ? std::string() : lines_.back();
}

void Job::stop() {
    stop_ = true;
}

void Job::run() {
    std::vector<const char*> argv;
    for (const auto& a : args_)
        argv.push_back(a.c_str());
    argv.push_back(nullptr);
    const std::string cwd = native_u8(working_directory_);

    const SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, argv.data());
    SDL_SetStringProperty(props, SDL_PROP_PROCESS_CREATE_WORKING_DIRECTORY_STRING, cwd.c_str());
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_NULL);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_APP);
    SDL_SetBooleanProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN, true);
    SDL_Process* process = SDL_CreateProcessWithProperties(props);
    SDL_DestroyProperties(props);
    if (process == nullptr) {
        std::lock_guard lock(mutex_);
        error_ = SDL_GetError();
        exit_code_ = -1;
        done_ = true;
        return;
    }

    SDL_IOStream* out = SDL_GetProcessOutput(process);
    std::string partial;
    char buffer[4096];
    int code = -1;
    for (;;) {
        if (stop_) {
            SDL_KillProcess(process, true);
        }
        const size_t n = out != nullptr ? SDL_ReadIO(out, buffer, sizeof(buffer)) : 0;
        if (n > 0) {
            partial.append(buffer, n);
            std::size_t at;
            while ((at = partial.find('\n')) != std::string::npos) {
                std::string line = partial.substr(0, at);
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();
                partial.erase(0, at + 1);
                std::lock_guard lock(mutex_);
                lines_.push_back(std::move(line));
            }
            continue;
        }
        if (SDL_WaitProcess(process, false, &code)) {
            // drain what is left
            while (out != nullptr) {
                const size_t m = SDL_ReadIO(out, buffer, sizeof(buffer));
                if (m == 0)
                    break;
                partial.append(buffer, m);
            }
            break;
        }
        if (out != nullptr && SDL_GetIOStatus(out) == SDL_IO_STATUS_EOF) {
            SDL_WaitProcess(process, true, &code);
            break;
        }
        SDL_Delay(20);
    }
    {
        std::lock_guard lock(mutex_);
        std::size_t start = 0;
        while (start < partial.size()) {
            const auto at = partial.find('\n', start);
            std::string line = partial.substr(start, at == std::string::npos ? std::string::npos : at - start);
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (!line.empty())
                lines_.push_back(std::move(line));
            if (at == std::string::npos)
                break;
            start = at + 1;
        }
    }
    SDL_DestroyProcess(process);
    exit_code_ = code;
    done_ = true;
}

std::unique_ptr<Job> start_port_dump(const fs::path& port_exe, const fs::path& root, const std::string& kind,
                                     const fs::path& directory) {
    return std::make_unique<Job>(std::vector<std::string>{native_u8(port_exe), "--dump-" + kind, native_u8(directory)},
                                 root);
}

}  // namespace studio
