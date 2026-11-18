#ifndef FILE_DIALOG_H
#define FILE_DIALOG_H

/* Non-blocking native dialog: open() starts it, draw() polls and returns true once on selection. */

#include <memory>
#include <string>
#include <vector>

namespace pfd { class open_file; }

class FileDialog
{
    public:
        void open(const std::string& title,
                  const std::string& initialPath = "",
                  const std::vector<std::string>& filter = { "All Files", "*" });

        // Closes a still-open native dialog so shutdown cannot block it
        ~FileDialog();

        /// Poll the dialog. @return true once, when a file was confirmed.
        bool draw();

        bool isOpen() const { return open_; }

        /// Full path of the last selected file
        std::string fileName() const { return fileName_; }

    private:

        bool open_ = false;
        std::string fileName_ = "";
        std::shared_ptr<pfd::open_file> dialog_;   // alive while the dialog is up

};

#endif // FILE_DIALOG_H
