#include "file_dialog.h"

#include "portable-file-dialogs.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <system_error>
#include <vector>

#ifndef _WIN32
    #include <pwd.h>
    #include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace
{

std::string homeDirectory()
{
#ifdef _WIN32
    if (const char* p = std::getenv("USERPROFILE")) return p;
#else
    if (const char* h = std::getenv("HOME"); h && *h) return h;
    if (const passwd* pw = getpwuid(getuid()); pw && pw->pw_dir && *pw->pw_dir)
        return pw->pw_dir;
#endif
    return ".";
}

// Start location argument for the native dialog.
//
// zenity 4 (GTK4) ignores a plain directory in --filename, but opens in the
// folder of a file path, even if that file does not exist. So on Linux we
// append a dummy file name. The name is not shown in an "open" dialog.
// Other platforms get the plain directory (with a trailing separator).
std::string startArg(const fs::path& dir, const std::string& lastFile)
{
#ifdef __linux__
    std::error_code ec;

    // Reopen next to (and highlighting) the last selected file, if it
    // still exists in this directory.
    if (!lastFile.empty() &&
        fs::path(lastFile).parent_path() == dir &&
        fs::is_regular_file(lastFile, ec))
    {
        return lastFile;
    }

    return (dir / "_select_file_").string();
#else
    (void)lastFile;
    return (dir / "").string();
#endif
}

// Convert a user friendly filter such as ".tle, 3le; *.txt" into a glob list
// such as "*.tle *.TLE *.3le *.3LE *.txt *.TXT". Returns "*" for "all files"
// (empty filter or a bare wildcard).
//
// GTK file filters are case-sensitive, so every extension is emitted in
// lowercase and uppercase (and as typed, if that is yet another spelling).
std::string toGlobs(const std::string& filter)
{
    std::string globs, token;

    auto addGlob = [&globs](const std::string& ext)
    {
        const std::string glob = "*." + ext;

        // Skip spellings that are already in the list (e.g. "123")
        const std::string list = " " + globs + " ";
        if (list.find(" " + glob + " ") != std::string::npos)
            return;

        if (!globs.empty()) globs += ' ';
        globs += glob;
    };

    // The trailing space flushes the last token
    for (char c : filter + ' ')
    {
        if (c != ',' && c != ';' && !std::isspace(static_cast<unsigned char>(c)))
        {
            token += c;
            continue;
        }

        if (token.empty())
            continue;

        // "*.tle", ".tle" and "tle" all become "tle"; "*" and "*.*" become ""
        token.erase(0, token.find_first_not_of("*."));

        if (token.empty())
            return "*";                    // wildcard wins

        std::string lower = token, upper = token;
        for (char& ch : lower) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        for (char& ch : upper) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));

        addGlob(lower);
        addGlob(upper);
        addGlob(token);                    // as typed, e.g. "Tle"

        token.clear();
    }

    return globs.empty() ? "*" : globs;
}

}

void FileDialog::open(const std::string& title,
                      const std::string& filter,
                      const std::string& initialPath)
{
    if (open_)
        return;

    if (!pfd::settings::available())
    {
        std::fprintf(stderr,
            "FileDialog: no native dialog backend found "
            "(Linux: install zenity or kdialog)\n");
        return;
    }

    std::error_code ec;
    fs::path start;

    if (!lastSelectedDirectory_.empty() && fs::is_directory(lastSelectedDirectory_, ec))
        start = lastSelectedDirectory_;
    else if (initialPath.empty())
        start = homeDirectory();
    else
    {
        fs::path p(initialPath);
        ec.clear();
        if (fs::is_directory(p, ec))
            start = p;
        else
        {
            start = p.parent_path();
            if (start.empty()) start = fs::current_path();
        }
    }

    const std::string globs = toGlobs(filter);

    std::vector<std::string> filters;
    if (globs != "*")
        filters = { "Supported files", globs };
    filters.insert(filters.end(), { "All files", "*" });

    dialog_ = std::make_shared<pfd::open_file>(title,
                                               startArg(start, fileNameSelected_),
                                               filters, pfd::opt::none);
    open_ = true;
}

bool FileDialog::draw()
{
    if (!open_ || !dialog_)
        return false;

    if (!dialog_->ready(250))        // blocks the render loop for 250 ms
        return false;

    std::vector<std::string> result = dialog_->result();
    dialog_.reset();
    open_ = false;

    if (result.empty())              // cancelled
        return false;

    fileNameSelected_ = result.front();
    lastSelectedDirectory_ = fs::path(fileNameSelected_).parent_path().string();
    return true;
}
