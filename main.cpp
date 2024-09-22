// Simple basecode showing how to create a window and attatch a vulkan surface
#define GATEWARE_ENABLE_CORE	 // All libraries need this
#define GATEWARE_ENABLE_SYSTEM	 // Graphics libs require system level libraries
#define GATEWARE_ENABLE_GRAPHICS // Enables all Graphics Libraries

// Ignore some graphics libraries we aren't going to use
#define GATEWARE_DISABLE_GDIRECTX11SURFACE // we have another template for this
#define GATEWARE_DISABLE_GDIRECTX12SURFACE // we have another template for this
#define GATEWARE_DISABLE_GRASTERSURFACE	   // we have another template for this
#define GATEWARE_DISABLE_GOPENGLSURFACE	   // we have another template for this

// tinyGLTF is a header only library, so we can include it directly
#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "TinyGLTF/tiny_gltf.h"

// With what we want & what we don't defined we can include the API
#include "Gateware.h"
#include "FileIntoString.h"	
#include "renderer.h"

// open some namespaces to compact the code a bit
using namespace GW;
using namespace CORE;
using namespace SYSTEM;
using namespace GRAPHICS;

// lets pop a window and use Vulkan to clear to a red screen
int main()
{
	GWindow win;
	GEventResponder msgs;
	GVulkanSurface vulkan;

	if (+win.Create(0, 0, 800, 600, GWindowStyle::WINDOWEDBORDERED))
	{
		win.SetWindowName("My Vulkan Work 1");
		VkClearValue clrAndDepth[2];
		clrAndDepth[0].color = {{0.529f, 0.0f, 0.016f, 1}}; // TODO: Part 1a (optional)
		clrAndDepth[1].depthStencil = {1.0f, 0u};
		msgs.Create([&](const GW::GEvent &e)
					{
			GW::SYSTEM::GWindow::Events q;
			if (+e.Read(q) && q == GWindow::Events::RESIZE)
				clrAndDepth[0].color.float32[2] += 0.01f; });
		win.Register(msgs);
#ifndef NDEBUG
		const char *debugLayers[] = {
			"VK_LAYER_KHRONOS_validation", // standard validation layer
		};
		if (+vulkan.Create(win, GW::GRAPHICS::DEPTH_BUFFER_SUPPORT,
						   sizeof(debugLayers) / sizeof(debugLayers[0]),
						   debugLayers, 0, nullptr, 0, nullptr, false))
#else
		if (+vulkan.Create(win, GW::GRAPHICS::DEPTH_BUFFER_SUPPORT))
#endif
		{
			Renderer renderer(win, vulkan);
			while (+win.ProcessWindowEvents())
			{
				if (+vulkan.StartFrame(2, clrAndDepth))
				{
					renderer.Render();
					vulkan.EndFrame(true);
				}
			}
		}
	}
	return 0; // that's all folks
}
