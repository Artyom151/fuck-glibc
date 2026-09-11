#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "runner.hpp"
#include "bootstrap.hpp"
#include "elf_parser.hpp"
#include "utils.hpp"

#include <sched.h>
#include <sys/mount.h>
#include <sys/syscall.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace fglibc {

static int pivot_root(const char *new_root, const char *put_old) {
    return syscall(SYS_pivot_root, new_root, put_old);
}

int Runner::run(const Config &cfg, const std::vector<std::string> &args, const std::string &custom_cwd) {
    if (args.empty()) {
        utils::log_error("No command or binary specified to run.");
        return 1;
    }

    if (!BootstrapManager::ensure_initialized(cfg)) {
        return 1;
    }

    // If pacman is installed in prefix, ensure ALPM database directories, version, and config are initialized
    std::string pacman_bin = cfg.sysroot_dir + "/usr/bin/pacman";
    if (utils::file_exists(pacman_bin)) {
        std::string alpm_dir = cfg.sysroot_dir + "/var/lib/pacman/local";
        utils::create_dirs(alpm_dir);
        utils::create_dirs(cfg.sysroot_dir + "/var/lib/pacman/sync");
        utils::create_dirs(cfg.sysroot_dir + "/var/cache/pacman/pkg");
        std::string db_ver = alpm_dir + "/ALPM_DB_VERSION";
        if (!utils::file_exists(db_ver)) {
            utils::write_text_file(db_ver, "9\n");
        }

        // Ensure DownloadUser = alpm and strict SigLevel don't break pacman on Alpine host
        std::string pconf = cfg.sysroot_dir + "/etc/pacman.conf";
        if (utils::file_exists(pconf)) {
            std::string content = utils::read_text_file(pconf);
            bool changed = false;
            size_t pos = content.find("DownloadUser = alpm");
            if (pos != std::string::npos && content.find("#DownloadUser = alpm") == std::string::npos) {
                content.replace(pos, std::string("DownloadUser = alpm").length(), "#DownloadUser = alpm");
                changed = true;
            }
            pos = content.find("SigLevel    = Required");
            if (pos != std::string::npos) {
                size_t eol = content.find('\n', pos);
                content.replace(pos, (eol != std::string::npos ? eol - pos : content.length() - pos), "SigLevel = Never");
                changed = true;
            }
            if (changed) {
                utils::write_text_file(pconf, content);
            }
        }

        // Ensure mirrorlist has active mirrors
        std::string mlist = cfg.sysroot_dir + "/etc/pacman.d/mirrorlist";
        if (utils::file_exists(mlist)) {
            std::string mcontent = utils::read_text_file(mlist);
            if (mcontent.find("Server = https://geo.mirror.pkgbuild.com") == std::string::npos) {
                mcontent = "Server = https://geo.mirror.pkgbuild.com/$repo/os/$arch\nServer = https://fastly.mirror.pkgbuild.com/$repo/os/$arch\n" + mcontent;
                utils::write_text_file(mlist, mcontent);
            }
        }
    }

    // Ensure valid SSL certificate bundle in sysroot so git/curl/pacman never fail on SSL
    std::string sys_crt = cfg.sysroot_dir + "/etc/ssl/certs/ca-certificates.crt";
    if (!utils::file_exists(sys_crt)) {
        if (utils::file_exists("/etc/ssl/certs/ca-certificates.crt")) {
            utils::create_dirs(cfg.sysroot_dir + "/etc/ssl/certs");
            utils::create_dirs(cfg.sysroot_dir + "/etc/ca-certificates/extracted");
            std::string crt_data = utils::read_text_file("/etc/ssl/certs/ca-certificates.crt");
            if (!crt_data.empty()) {
                utils::write_text_file(sys_crt, crt_data);
                utils::write_text_file(cfg.sysroot_dir + "/etc/ca-certificates/extracted/tls-ca-bundle.pem", crt_data);
            }
        }
    }

    std::string target_cwd = custom_cwd.empty() ? utils::get_cwd() : custom_cwd;

    switch (cfg.engine) {
        case EngineType::NAMESPACE:
            return run_namespace_engine(cfg, args, target_cwd);
        case EngineType::BWRAP:
            return run_bwrap_engine(cfg, args, target_cwd);
        case EngineType::DIRECT:
            return run_direct_engine(cfg, args, target_cwd);
    }
    return 1;
}

int Runner::run_shell(const Config &cfg) {
    std::string shell_bin = "/bin/sh";
    if (utils::file_exists("/bin/bash")) {
        shell_bin = "/bin/bash";
    }

    std::cout << utils::colors::CYAN << utils::colors::BOLD
              << "\n=== Entering fglibc Interactive Shell ===\n"
              << "Type 'exit' to return to host Alpine environment.\n"
              << utils::colors::RESET << std::endl;

    std::vector<std::string> args = { shell_bin };
    return run(cfg, args);
}

int Runner::run_namespace_engine(const Config &cfg, const std::vector<std::string> &args, const std::string &target_cwd) {
    uid_t uid = getuid();
    gid_t gid = getgid();

    pid_t child = fork();
    if (child < 0) {
        utils::log_error("fork() failed");
        return 1;
    }

    if (child > 0) {
        int status = 0;
        waitpid(child, &status, 0);
        if (WIFEXITED(status)) {
            return WEXITSTATUS(status);
        } else if (WIFSIGNALED(status)) {
            return 128 + WTERMSIG(status);
        }
        return status;
    }

    // Child process: unshare user and mount namespace
    if (unshare(CLONE_NEWUSER | CLONE_NEWNS) < 0) {
        perror("unshare(CLONE_NEWUSER | CLONE_NEWNS)");
        utils::log_warn("Falling back to direct dynamic linker execution...");
        _exit(run_direct_engine(cfg, args, target_cwd));
    }

    // Write uid/gid maps
    utils::write_text_file("/proc/self/setgroups", "deny");
    char id_buf[64];
    snprintf(id_buf, sizeof(id_buf), "%d %d 1\n", uid, uid);
    utils::write_text_file("/proc/self/uid_map", id_buf);
    snprintf(id_buf, sizeof(id_buf), "%d %d 1\n", gid, gid);
    utils::write_text_file("/proc/self/gid_map", id_buf);

    mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL);

    char newroot[256];
    snprintf(newroot, sizeof(newroot), "/tmp/fglibc-root-%d", getpid());
    mkdir(newroot, 0755);

    if (mount("tmpfs", newroot, "tmpfs", 0, NULL) < 0) {
        perror("mount tmpfs newroot");
        _exit(1);
    }

    char oldroot[300];
    snprintf(oldroot, sizeof(oldroot), "%s/oldroot", newroot);
    mkdir(oldroot, 0755);

    const char *skeleton_dirs[] = {
        "dev", "proc", "sys", "etc", "tmp", "run", "home", "root",
        "usr", "usr/lib", "usr/lib32", "usr/bin", "usr/share", "usr/libexec",
        "lib", "lib64", "lib32", "bin", "sbin", "opt", "var", "mnt", "media", NULL
    };
    for (int i = 0; skeleton_dirs[i]; i++) {
        char p[350];
        snprintf(p, sizeof(p), "%s/%s", newroot, skeleton_dirs[i]);
        mkdir(p, 0755);
    }

    if (pivot_root(newroot, oldroot) < 0) {
        perror("pivot_root");
        _exit(1);
    }

    // Mount host subsystems
    mount("/oldroot/proc", "/proc", NULL, MS_BIND | MS_REC, NULL);
    mount("/oldroot/sys",  "/sys",  NULL, MS_BIND | MS_REC, NULL);
    mount("/oldroot/dev",  "/dev",  NULL, MS_BIND | MS_REC, NULL);
    mount("/oldroot/tmp",  "/tmp",  NULL, MS_BIND | MS_REC, NULL);
    mount("/oldroot/run",  "/run",  NULL, MS_BIND | MS_REC, NULL);
    mount("/oldroot/home", "/home", NULL, MS_BIND | MS_REC, NULL);
    mount("/oldroot/root", "/root", NULL, MS_BIND | MS_REC, NULL);
    mount("/oldroot/sbin", "/sbin", NULL, MS_BIND | MS_REC, NULL);
    std::string old_sysroot = std::string("/oldroot") + cfg.sysroot_dir;

    // Overlay /var with sysroot/var (pacman db, cache)
    if (utils::dir_exists(old_sysroot + "/var")) {
        std::string ovl_var = "lowerdir=" + old_sysroot + "/var:/oldroot/var";
        if (mount("overlay", "/var", "overlay", 0, ovl_var.c_str()) < 0) {
            mount("/oldroot/var", "/var", NULL, MS_BIND | MS_REC, NULL);
            if (utils::dir_exists(old_sysroot + "/var/lib/pacman")) {
                mkdir("/var/lib", 0755);
                mkdir("/var/lib/pacman", 0755);
                mount((old_sysroot + "/var/lib/pacman").c_str(), "/var/lib/pacman", NULL, MS_BIND | MS_REC, NULL);
            }
        }
    } else {
        mount("/oldroot/var", "/var", NULL, MS_BIND | MS_REC, NULL);
    }

    // Overlay /bin so glibc tools (bash, sh, coreutils) take precedence over host musl /bin
    if (utils::dir_exists(old_sysroot + "/usr/bin")) {
        std::string ovl_bin = "lowerdir=" + old_sysroot + "/usr/bin";
        if (utils::dir_exists(old_sysroot + "/bin")) {
            ovl_bin += ":" + old_sysroot + "/bin";
        }
        ovl_bin += ":/oldroot/bin";
        if (mount("overlay", "/bin", "overlay", 0, ovl_bin.c_str()) < 0) {
            mount("/oldroot/bin", "/bin", NULL, MS_BIND | MS_REC, NULL);
        }
    } else {
        mount("/oldroot/bin", "/bin", NULL, MS_BIND | MS_REC, NULL);
    }

    // Overlay sysroot/usr/bin with host /usr/bin so installed glibc tools are available
    if (utils::dir_exists(old_sysroot + "/usr/bin")) {
        std::string ovl_opt = "lowerdir=" + old_sysroot + "/usr/bin:/oldroot/usr/bin";
        if (mount("overlay", "/usr/bin", "overlay", 0, ovl_opt.c_str()) < 0) {
            mount("/oldroot/usr/bin", "/usr/bin", NULL, MS_BIND | MS_REC, NULL);
        }
    } else {
        mount("/oldroot/usr/bin", "/usr/bin", NULL, MS_BIND | MS_REC, NULL);
    }

    // Overlay sysroot/usr/share with host /usr/share (fonts, icons, themes, app assets)
    if (utils::dir_exists(old_sysroot + "/usr/share")) {
        std::string ovl_opt = "lowerdir=" + old_sysroot + "/usr/share:/oldroot/usr/share";
        if (mount("overlay", "/usr/share", "overlay", 0, ovl_opt.c_str()) < 0) {
            mount("/oldroot/usr/share", "/usr/share", NULL, MS_BIND | MS_REC, NULL);
        }
    } else {
        mount("/oldroot/usr/share", "/usr/share", NULL, MS_BIND | MS_REC, NULL);
    }

    // Mount /opt if exists
    if (utils::dir_exists(old_sysroot + "/opt") && utils::dir_exists("/oldroot/opt")) {
        std::string ovl_opt = "lowerdir=" + old_sysroot + "/opt:/oldroot/opt";
        if (mount("overlay", "/opt", "overlay", 0, ovl_opt.c_str()) < 0) {
            mount((old_sysroot + "/opt").c_str(), "/opt", NULL, MS_BIND | MS_REC, NULL);
        }
    } else if (utils::dir_exists(old_sysroot + "/opt")) {
        mount((old_sysroot + "/opt").c_str(), "/opt", NULL, MS_BIND | MS_REC, NULL);
    }

    if (utils::dir_exists("/oldroot/usr/libexec")) {
        mount("/oldroot/usr/libexec", "/usr/libexec", NULL, MS_BIND | MS_REC, NULL);
    }

    // Bind glibc sysroot libraries to /lib, /lib64, and /usr/lib so dlopen never finds host musl/gcompat libs
    mount((old_sysroot + "/usr/lib").c_str(), "/lib", NULL, MS_BIND | MS_REC, NULL);
    mount((old_sysroot + "/usr/lib").c_str(), "/lib64", NULL, MS_BIND | MS_REC, NULL);
    mount((old_sysroot + "/usr/lib").c_str(), "/usr/lib", NULL, MS_BIND | MS_REC, NULL);

    // Bind 32-bit libraries for Steam / Wine / 32-bit compatibility if present
    if (utils::dir_exists(old_sysroot + "/usr/lib32")) {
        mount((old_sysroot + "/usr/lib32").c_str(), "/usr/lib32", NULL, MS_BIND | MS_REC, NULL);
        mount((old_sysroot + "/usr/lib32").c_str(), "/lib32", NULL, MS_BIND | MS_REC, NULL);
    }

    // If /lib/ld-linux-x86-64.so.2 exists in /lib, bind glibc dynamic linker over it
    std::string glibc_ld = old_sysroot + "/usr/lib/ld-linux-x86-64.so.2";
    if (utils::file_exists("/lib/ld-linux-x86-64.so.2") && utils::file_exists(glibc_ld)) {
        mount(glibc_ld.c_str(), "/lib/ld-linux-x86-64.so.2", NULL, MS_BIND, NULL);
    }

    // 32-bit dynamic linker
    std::string glibc_ld32 = old_sysroot + "/usr/lib32/ld-linux.so.2";
    if (utils::file_exists(glibc_ld32)) {
        if (utils::file_exists("/lib/ld-linux.so.2")) {
            mount(glibc_ld32.c_str(), "/lib/ld-linux.so.2", NULL, MS_BIND, NULL);
        }
    }

    // Overlay /etc with sysroot/etc so pacman.conf, makepkg.conf, ld.so.cache take precedence over host /etc
    if (utils::dir_exists(old_sysroot + "/etc")) {
        std::string ovl_etc = "lowerdir=" + old_sysroot + "/etc:/oldroot/etc";
        if (mount("overlay", "/etc", "overlay", 0, ovl_etc.c_str()) < 0) {
            mount("/oldroot/etc", "/etc", NULL, MS_BIND | MS_REC, NULL);
            std::string sysroot_cache = old_sysroot + "/etc/ld.so.cache";
            if (utils::file_exists(sysroot_cache)) {
                mount(sysroot_cache.c_str(), "/etc/ld.so.cache", NULL, MS_BIND, NULL);
            }
        }
    } else {
        mount("/oldroot/etc", "/etc", NULL, MS_BIND | MS_REC, NULL);
    }

    umount2("/oldroot", MNT_DETACH);

    if (chdir(target_cwd.c_str()) != 0) {
        chdir("/");
    }

    setenv("FGLIBC_ENV", "1", 1);
    setenv("LIBGL_DRIVERS_PATH", "/lib64/dri", 1);
    setenv("PS1", "[fglibc-env] \\w $ ", 1);

    std::string new_path = "/usr/local/bin:/usr/bin:/bin";
    const char *cur_path = getenv("PATH");
    if (cur_path && *cur_path) {
        new_path += ":" + std::string(cur_path);
    }
    new_path = cfg.sysroot_dir + "/usr/bin:" + cfg.sysroot_dir + "/bin:" + new_path;
    setenv("PATH", new_path.c_str(), 1);

    std::string egl_vendor = cfg.sysroot_dir + "/usr/share/glvnd/egl_vendor.d/50_mesa.json";
    if (utils::file_exists(egl_vendor)) {
        setenv("__EGL_VENDOR_LIBRARY_FILENAMES", egl_vendor.c_str(), 1);
        setenv("__EGL_VENDOR_LIBRARY_DIRS", (cfg.sysroot_dir + "/usr/share/glvnd/egl_vendor.d").c_str(), 1);
    }

    if (!cfg.custom_library_path.empty()) {
        const char *cur_lp = getenv("LD_LIBRARY_PATH");
        std::string new_lp = cfg.custom_library_path;
        if (cur_lp && *cur_lp) {
            new_lp += ":" + std::string(cur_lp);
        }
        setenv("LD_LIBRARY_PATH", new_lp.c_str(), 1);
    } else {
        unsetenv("LD_LIBRARY_PATH");
    }

    std::vector<char *> exec_argv;
    for (const auto &arg : args) {
        exec_argv.push_back(const_cast<char *>(arg.c_str()));
    }
    exec_argv.push_back(nullptr);

    if (cfg.debug) {
        utils::log_debug("Executing target inside namespace: " + args[0]);
    }

    execvp(exec_argv[0], exec_argv.data());
    perror("execvp failed");
    _exit(127);
}

int Runner::run_bwrap_engine(const Config &cfg, const std::vector<std::string> &args, const std::string &target_cwd) {
    if (!utils::file_exists("/usr/bin/bwrap") && !utils::file_exists("/bin/bwrap")) {
        utils::log_error("Bubblewrap (bwrap) is not installed on host!");
        return 1;
    }

    std::vector<std::string> cmd = {
        "bwrap",
        "--ro-bind", "/", "/",
        "--dev", "/dev",
        "--proc", "/proc",
        "--ro-bind", cfg.sysroot_dir + "/usr/lib", "/lib64",
        "--bind", "/tmp", "/tmp",
        "--bind", utils::get_home_dir(), utils::get_home_dir(),
        "--chdir", target_cwd
    };

    if (cfg.passthrough_dri && utils::dir_exists("/dev/dri")) {
        cmd.push_back("--dev-bind");
        cmd.push_back("/dev/dri");
        cmd.push_back("/dev/dri");
    }

    if (utils::dir_exists("/run")) {
        cmd.push_back("--bind");
        cmd.push_back("/run");
        cmd.push_back("/run");
    }

    cmd.push_back("--setenv");
    cmd.push_back("FGLIBC_ENV");
    cmd.push_back("1");
    cmd.push_back("--setenv");
    cmd.push_back("LD_LIBRARY_PATH");
    cmd.push_back("/lib64");

    for (const auto &a : args) {
        cmd.push_back(a);
    }

    std::vector<char *> exec_argv;
    for (const auto &c : cmd) {
        exec_argv.push_back(const_cast<char *>(c.c_str()));
    }
    exec_argv.push_back(nullptr);

    execvp("bwrap", exec_argv.data());
    perror("execvp bwrap failed");
    return 1;
}

int Runner::run_direct_engine(const Config &cfg, const std::vector<std::string> &args, const std::string &target_cwd) {
    std::string ld_path = cfg.get_ld_linux_path();
    if (ld_path.empty()) {
        utils::log_error("Dynamic linker not found in prefix!");
        return 1;
    }

    std::string libpath = cfg.sysroot_dir + "/usr/lib:" + cfg.sysroot_dir + "/lib64:" + cfg.sysroot_dir + "/lib";
    if (!cfg.custom_library_path.empty()) {
        libpath = cfg.custom_library_path + ":" + libpath;
    }

    std::vector<std::string> cmd = {
        ld_path,
        "--library-path", libpath
    };

    for (const auto &a : args) {
        cmd.push_back(a);
    }

    std::vector<char *> exec_argv;
    for (const auto &c : cmd) {
        exec_argv.push_back(const_cast<char *>(c.c_str()));
    }
    exec_argv.push_back(nullptr);

    chdir(target_cwd.c_str());
    execv(ld_path.c_str(), exec_argv.data());
    perror("execv ld-linux failed");
    return 1;
}

} // namespace fglibc
