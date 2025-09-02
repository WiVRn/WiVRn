#include "renderdoc.h"
#include "renderdoc_app.h"

#include <dlfcn.h>
#include <iostream>

static RENDERDOC_API_1_0_0 * renderdoc_init()
{
	RENDERDOC_API_1_0_0 * renderdoc_api = nullptr;
	auto renderdoc_module = dlopen("librenderdoc.so", RTLD_NOW | RTLD_NOLOAD);
	if (not renderdoc_module)
		return nullptr;
	auto * func = reinterpret_cast<pRENDERDOC_GetAPI>(dlsym(renderdoc_module, "RENDERDOC_GetAPI"));
	if (!func)
	{
		std::cerr << "Failed to load RENDERDOC_GetAPI function." << std::endl;
		return nullptr;
	}
	if (!func(eRENDERDOC_API_Version_1_0_0, reinterpret_cast<void **>(&renderdoc_api)))
	{
		std::cerr << "Failed to obtain RenderDoc 1.0.0 API." << std::endl;
		return nullptr;
	}

	int major, minor, patch;
	renderdoc_api->GetAPIVersion(&major, &minor, &patch);
	return renderdoc_api;
}

static RENDERDOC_API_1_0_0 * get_renderdoc()
{
	static RENDERDOC_API_1_0_0 * renderdoc_api = renderdoc_init();
	return renderdoc_api;
}

void wivrn::renderdoc_begin(VkInstance inst)
{
	if (auto renderdoc_api = get_renderdoc())
		renderdoc_api->StartFrameCapture(RENDERDOC_DEVICEPOINTER_FROM_VKINSTANCE(inst), nullptr);
}
void wivrn::renderdoc_end(VkInstance inst)
{
	if (auto renderdoc_api = get_renderdoc())
		renderdoc_api->EndFrameCapture(RENDERDOC_DEVICEPOINTER_FROM_VKINSTANCE(inst), nullptr);
}
