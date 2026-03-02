# Contributing to TLEscope

Thank you for your interest in contributing. TLEscope is built for performance and clarity. To maintain the project's "minimal footprint" philosophy, please adhere to the following guidelines.

---

## **Workflow**

1. **Check Issues**: Before starting work, check the [Issue Tracker](https://github.com/aweeri/TLEscope/issues) to ensure the task isn't already being handled.
2. **Open an Issue**: For major features or breaking changes, open an issue first to discuss the implementation approach.
3. **Fork & Branch**: Fork the repository and create a descriptive branch.
4. **Pull Request**: Submit a PR against the `main` branch. Provide a concise summary of changes and reference any related issues.

---

## **Coding Standards**

TLEscope is written in **pure C** using the **Raylib/RayGui** framework. 

* **Keep it Lightweight**: Avoid adding heavy external dependencies unles absolutely neccessary. If it can be done in pure C or with Raylib, do it there.
* **Performance First**: Code should be efficient. Avoid unnecessary allocations in the main render loop.
* **Code Style**: 
    * Maintain existing naming conventions and lettering styles.
    * Use clear, descriptive variable names.
    * Keep functions focused and modular.
* **Comments**: Document complex orbital math or non-obvious logic. Follow the existing commenting style to keep the codebase cohesive.

---

## **Building and Testing**

Ensure your changes compile on your native OS before submitting. If you have the toolchain available, verify the cross-compilation for Windows.

---

## **Reporting Bugs**

When reporting a bug, please include:
* Your Operating System (Distro/Windows version).
* Steps to reproduce the visual or functional glitch.
* Log output if available.
