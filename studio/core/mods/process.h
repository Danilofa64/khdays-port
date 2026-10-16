// Running another program -- the port's dumps, the modder's ffmpeg
// -- on a thread of its own, through SDL3 (portable), its
// output gathered line by line for the interface to show.
#pragma once

#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace studio {

class Job {
public:
    // Starts `args` (args[0] the program) in `working_directory`.
    Job(std::vector<std::string> args, std::filesystem::path working_directory);
    ~Job();
    Job(const Job&) = delete;
    Job& operator=(const Job&) = delete;

    bool done() const {
        return done_.load();
    }
    int exit_code() const {
        return exit_code_.load();
    }                                        // valid once done; -1 when it could not start
    std::string error() const;               // why it could not start
    std::vector<std::string> lines() const;  // its output so far
    std::string last_line() const;           // the latest line of it
    void stop();                             // ends it (killed)

private:
    void run();

    std::vector<std::string> args_;
    std::filesystem::path working_directory_;
    mutable std::mutex mutex_;
    std::vector<std::string> lines_;
    std::string error_;
    std::atomic<bool> done_{false};
    std::atomic<bool> stop_{false};
    std::atomic<int> exit_code_{-1};
    std::thread thread_;
};

// The port's dump of `kind` ("graphics" or "sounds") into `directory`: the
// port's executable run from the port's folder (port/runtime/core/start.c: it
// writes the dump without starting the game).
std::unique_ptr<Job> start_port_dump(const std::filesystem::path& port_exe, const std::filesystem::path& root,
                                     const std::string& kind, const std::filesystem::path& directory);

}  // namespace studio
