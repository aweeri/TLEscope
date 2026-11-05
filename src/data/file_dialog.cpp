#include "file_dialog.h"
#include "portable-file-dialogs.h"

#include <cstdio>

namespace
{
std::vector<std::string> sanitizeFilter(const std::vector<std::string>& filter)
{
    std::vector<std::string> out;
    bool hasAllFiles = false;

    for (size_t i = 0; i + 1 < filter.size(); i += 2)
    {
        const std::string& name = filter[i];
        const std::string& pattern = filter[i + 1];

        if (name.empty() || pattern.empty())
            continue;

        if (pattern == "*" || pattern == "*.*")
            hasAllFiles = true;

        out.push_back(name);
        out.push_back(pattern);
    }

    if (out.empty())
        return { "All Files", "*" };

    if (!hasAllFiles)
    {
        out.push_back("All Files");
        out.push_back("*");
    }

    return out;
}
} // namespace

FileDialog::~FileDialog()
{
    // Close a still-open dialog so app shutdown never hangs
    if (dialog_)
    {
        dialog_->kill();
        dialog_.reset();
    }
    open_ = false;
}

void FileDialog::open(const std::string& title,
                      const std::string& initialPath,
                      const std::vector<std::string>& filter)
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

    // Start in the directory of the last selection, otherwise at the user's home dir
    std::string startPath = initialPath;

    if (!fileName_.empty())
    {
        const size_t sep = fileName_.find_last_of("/\\");
        startPath = (sep == std::string::npos) ? pfd::path::home()
                                               : fileName_.substr(0, sep);
    }
    if (startPath.empty())
        startPath = pfd::path::home();

    dialog_ = std::make_shared<pfd::open_file>(title, startPath,
                                               sanitizeFilter(filter),
                                               pfd::opt::none);
    open_ = true;
}

bool FileDialog::draw()
{
    if (!open_ || !dialog_)
        return false;

    if (!dialog_->ready(250))        // poll without blocking the render loop
        return false;

    std::vector<std::string> result = dialog_->result();
    dialog_.reset();
    open_ = false;

    if (result.empty())              // cancelled
        return false;

    fileName_ = result.front();

    return true;
}
