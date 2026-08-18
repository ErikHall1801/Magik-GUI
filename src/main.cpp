// GLAD must come first ! Or else we get conflicts 
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <iostream>

void frame_buffer_size_callback(GLFWwindow* window, int width, int height);

void mouse_position_callback(GLFWwindow* window, double x_pos, double y_pos);

int main()
{
	// Init GLFW lib
	if (!glfwInit()) { return -1; }

	// Tells glfw that we use version 3.3 of openGL
	// So that if someone does not have this version the window creation
	// will fail. 
	// The "CORE_PROFILE" means we will not use backwards compatible features. 
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

	// Create window
	int height = 1000;
	int width = 1400;
	GLFWwindow* window = glfwCreateWindow(width, height, "Test application", nullptr, nullptr);

	if (!window)
	{
		printf("Failed to create window ! \n");
		glfwTerminate();
		return -1;
	}

	// Make context current ?
	// Means the OpenGL rendering context is tied to the window
	// Once the context is current, we can issue commands like
	// clearing the buffer, drawing geometry etc. Since OpenGL 
	// is a state machine
	glfwMakeContextCurrent(window);

	// Listen to framebuffer size events
	// I see so this defines a function which we call in the event the 
	// window, or really frame buffer, is resized. And this function can
	// do anything, i assume.
	glfwSetFramebufferSizeCallback(window, frame_buffer_size_callback);
	glfwSetCursorPosCallback(window, mouse_position_callback);

	// Load OpenGL function pointers
	if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
	{
		printf("Failed to load OpenGL function pointers ! \n");
		glfwTerminate();
		return -1;
	}

	double x_pos = 1.0, y_pos = 1.0;
	int x_size = 1, y_size = 1;

	// Render loop
	while (!glfwWindowShouldClose(window))
	{
		// This this button is pressed, do this
		if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
		{
			glfwSetWindowShouldClose(window, true);
		}

		// Clears the color framebuffer so we can draw on it
		if (glfwGetWindowAttrib(window, GLFW_HOVERED))
		{
			glfwGetCursorPos(window, &x_pos, &y_pos);
			glfwGetWindowSize(window, &x_size, &y_size);
		}

		glClearColor(x_pos/(double)x_size, y_pos/(double)y_size, 0.0f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);

		// Same as Vulkan swapchain ? 
		glfwSwapBuffers(window);

		// Pulls events like keyboard or mouse inputs 
		glfwPollEvents();
	}

	glfwTerminate();
	return 0;
}

void frame_buffer_size_callback(GLFWwindow* window, int width, int height)
{
	glViewport(0, 0, width, height); //Resetting the viewport for OpenGL 
	printf("Window size is %i x %i \n", width, height);
}

void mouse_position_callback(GLFWwindow* window, double x_pos, double y_pos)
{
	printf("Mouse position is %f x %f \n", x_pos, y_pos);
}