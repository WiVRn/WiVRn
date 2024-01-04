# Taken from FindVulkan.cmake, copied here because Android's cmake is too old

if(CMAKE_VERSION VERSION_LESS "3.23.0")
  # detect version e.g 1.2.189
  set(Vulkan_VERSION "")
  if(Vulkan_INCLUDE_DIR)
    set(VULKAN_CORE_H ${Vulkan_INCLUDE_DIR}/vulkan/vulkan_core.h)
    if(EXISTS ${VULKAN_CORE_H})
      file(STRINGS  ${VULKAN_CORE_H} VulkanHeaderVersionLine REGEX "^#define VK_HEADER_VERSION ")
      string(REGEX MATCHALL "[0-9]+" VulkanHeaderVersion "${VulkanHeaderVersionLine}")
      file(STRINGS  ${VULKAN_CORE_H} VulkanHeaderVersionLine2 REGEX "^#define VK_HEADER_VERSION_COMPLETE ")
      string(REGEX MATCHALL "[0-9]+" VulkanHeaderVersion2 "${VulkanHeaderVersionLine2}")
      list(LENGTH VulkanHeaderVersion2 _len)
      #  versions >= 1.2.175 have an additional numbers in front of e.g. '0, 1, 2' instead of '1, 2'
      if(_len EQUAL 3)
          list(REMOVE_AT VulkanHeaderVersion2 0)
      endif()
      list(APPEND VulkanHeaderVersion2 ${VulkanHeaderVersion})
      list(JOIN VulkanHeaderVersion2 "." Vulkan_VERSION)
    endif()
  endif()
endif()
