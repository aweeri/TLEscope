#include "file_dialog.h"
#include "portable-file-dialogs.h"

void FileDialog::open(const std::string& title,
                      const std::string& initialPath,
                      const std::vector<std::string> filter)
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

    // Configure start directory of file dialog

    std::string startDir;

    if (fileName_.empty())
        startDir = pfd::path::home() + pfd::path::separator() + "dummy.txt";
    else
        startDir = fileName();

    // Open the dialog

    dialog_ = std::make_shared<pfd::open_file>(title,startDir,
                                               filter, pfd::opt::none);
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

    fileName_ = result.front();

    return true;
}
