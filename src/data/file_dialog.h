#ifndef FILE_DIALOG_H
#define FILE_DIALOG_H

/**
 * @file file_dialog.h
 * @brief OS-native file dialog (via portable-file-dialogs).
 *
 * Non-blocking: open() starts the native dialog, draw() polls it every frame
 * and returns true in the one frame in which a file was chosen.
 *
 * The title and the file filter are given per open() call, so one FileDialog
 * object can be reused for different file types.
 */

#include <memory>
#include <string>

namespace pfd { class open_file; }

class FileDialog
{
    public:
        /**
         * Open the native file dialog. Does nothing if already open or if no
         * dialog backend is available.
         *
         * @param title       Dialog window title.
         * @param filter      Extensions to show, separated by commas, semicolons
         *                    or spaces, e.g. ".tle, .3le, .txt". Dots and
         *                    wildcards are optional (".tle", "*.tle" and "tle"
         *                    are equivalent). Empty or "*" = all files. An
         *                    "All files" entry is always offered as well.
         * @param initialPath Start directory (or file) used if no file has been
         *                    selected before; empty = home directory. After a
         *                    selection, the last used directory takes priority.
         */
        void open(const std::string& title,
                  const std::string& filter = "*",
                  const std::string& initialPath = "");

        /// Poll the dialog. @return true once, when a file was confirmed.
        bool draw();

        bool isOpen() const { return open_; }

        /// Full path of the last selected file (empty if none yet).
        std::string fileName() const { return fileNameSelected_; }

    private:
        bool open_ = false;
        std::shared_ptr<pfd::open_file> dialog_;   // alive while the dialog is up
        std::string lastSelectedDirectory_;
        std::string fileNameSelected_;
};

#endif // FILE_DIALOG_H
