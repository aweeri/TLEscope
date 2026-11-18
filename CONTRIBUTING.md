# Contributing to TLEscope

Thank you for your interest in contributing. TLEscope is built for performance and clarity. To maintain the project's "minimal footprint" philosophy, please adhere to the following guidelines.

---

## **Workflow**

1. **Check Issues**: Before starting work, check the [Issue Tracker](https://github.com/aweeri/TLEscope/issues) to ensure the task isn't already being handled.
2. **Open an Issue**: For major features or breaking changes, open an issue first to discuss the implementation approach.
3. **Fork & Branch**: Fork the repository and make your changes.
4. **Pull Request**: Submit a PR against the `main` branch. Provide a concise summary of changes and reference any related issues.

---

## **Coding Standards**

TLEscope is written in C++ using the **Raylib** and **Dear ImGui** frameworks. 

* **Keep it Lightweight**: Avoid adding heavy external dependencies unless absolutely neccessary. If it can be done in pure C++ or with Raylib, do it there.
* **Performance First**: Code should be efficient. Avoid unnecessary allocations in the main render loop.
* **Code Style**: 
    * Maintain existing naming conventions and lettering styles.
    * Use clear, descriptive variable names.
    * Keep functions focused and modular.
* **Comments**:
    * Write very short, clear sentences. Avoid multi-paragraph explanations at ALL costs.
    * Document non-obvious intent, math formulas, or engine workarounds.
    * Do not leave verbose, tutorial-style comment blocks or restate function signatures that are already clear from the code.
    * Limit markers to `TODO` and `WIP`: with a brief reason, and avoid leaving unresolved ones in finished pull requests as much as you can.
    * Do not commit generic AI patterns such as em-dashes and such.


## Our Stance on AI

TLEscope is a passion project that has been in the works for over 5 years - starting from a simple prototype written in Python, through many iterations in C, and now C++. Throughout its development, AI has been used strictly to assist with engineering, not replace it.

Many projects outright refuse AI-assisted changes, while others generate entire codebases without manually writing a single line. **TLEscope is neither of the above.**

LLMs are a great tool. They help newer programmers learn and contribute to projects they care about, and they can be a major asset to open-source software when used responsibly.

If you use AI as part of your contributions, we ask that you:
- Make small, chunked, easily verifiable changes.
- Search and understand the existing codebase first to avoid submitting duplicate or redundant logic.
- Review and understand every line you submit. Never open a pull request with code you cannot explain or defend during a code review.
- Keep the existing style and architecture intact. Do not let an LLM refactor unrelated files, introduce unnecessary dependencies, or override the project's established conventions.
- Write your own PR descriptions. Clearly explain the problem you solved and why your approach makes sense, rather than pasting a bloated AI-generated summary.
- Are honest about your AI use.

---

## **Building and Testing**

Ensure your changes compile on your native OS before submitting. If you have the toolchain available, verify the cross-compilation for Windows.

---

## **Reporting Bugs**

When reporting a bug, please include:
* Your Operating System (Distro/Windows version).
* Steps to reproduce the visual or functional glitch.
* Log output if available.
