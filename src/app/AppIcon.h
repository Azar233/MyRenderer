#pragma once

struct GLFWwindow;

// Sets a stable Windows taskbar identity before the first application window exists.
void initializeMyRendererApplicationIdentity();

// Applies the project icon to the title bar, taskbar, and app switcher.
void setMyRendererWindowIcon(GLFWwindow* window);
