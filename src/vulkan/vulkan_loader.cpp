#include <tuple>

#include "vulkan_loader.h"

#include "../util/log/log.h"

#include "../util/util_string.h"
#include "../util/util_win32_compat.h"

#ifdef __ANDROID__
#include <dlfcn.h>
#include <cstdlib>
#endif

namespace dxvk::vk {

  static std::pair<HMODULE, PFN_vkGetInstanceProcAddr> loadVulkanLibrary() {
#ifdef __ANDROID__
    // FORCED: Use pre-loaded Turnip driver via environment variables
    // DO NOT load system libvulkan.so - it always picks Adreno
    Logger::info("=== DXVK Vulkan Loader (Turnip Forced) ===");
    
    const char* vulkanPtrStr = getenv("VULKAN_PTR");
    const char* vkGetInstanceProcAddrStr = getenv("VK_GET_INSTANCE_PROC_ADDR");
    
    Logger::info(str::format("VULKAN_PTR env = ", vulkanPtrStr ? vulkanPtrStr : "(null)"));
    Logger::info(str::format("VK_GET_INSTANCE_PROC_ADDR env = ", vkGetInstanceProcAddrStr ? vkGetInstanceProcAddrStr : "(null)"));
    
    if (!vulkanPtrStr || !vkGetInstanceProcAddrStr) {
      Logger::err("Vulkan: TURNIP environment variables not set! Cannot continue.");
      Logger::err("Vulkan: Please ensure TurnipLoader runs before DXVK initialization.");
      return { };
    }
    
    uintptr_t vulkanHandle = 0;
    uintptr_t procAddr = 0;
    
    // Parse 64-bit hex values (with or without 0x prefix)
    if (vulkanPtrStr[0] == '0' && (vulkanPtrStr[1] == 'x' || vulkanPtrStr[1] == 'X')) {
      vulkanHandle = static_cast<uintptr_t>(std::strtoull(vulkanPtrStr + 2, nullptr, 16));
    } else {
      vulkanHandle = static_cast<uintptr_t>(std::strtoull(vulkanPtrStr, nullptr, 16));
    }
    
    if (vkGetInstanceProcAddrStr[0] == '0' && (vkGetInstanceProcAddrStr[1] == 'x' || vkGetInstanceProcAddrStr[1] == 'X')) {
      procAddr = static_cast<uintptr_t>(std::strtoull(vkGetInstanceProcAddrStr + 2, nullptr, 16));
    } else {
      procAddr = static_cast<uintptr_t>(std::strtoull(vkGetInstanceProcAddrStr, nullptr, 16));
    }
    
    Logger::info(str::format("Parsed VULKAN_PTR = 0x", std::hex, vulkanHandle));
    Logger::info(str::format("Parsed VK_GET_INSTANCE_PROC_ADDR = 0x", std::hex, procAddr));
    
    if (!vulkanHandle || !procAddr) {
      Logger::err("Vulkan: Failed to parse Turnip handles! Values are zero.");
      return { };
    }
    
    HMODULE library = reinterpret_cast<HMODULE>(vulkanHandle);
    auto proc = reinterpret_cast<PFN_vkGetInstanceProcAddr>(procAddr);
    
    Logger::info("=== Using TURNIP driver directly! ===");
    return std::make_pair(library, proc);
#else
    static const std::array<const char*, 3> dllNames = {{
#ifdef _WIN32
      "winevulkan.dll",
      "vulkan-1.dll",
#else
      "libvulkan.so",
      "libvulkan.so.1",
#endif
    }};

    for (auto dllName : dllNames) {
      HMODULE library = LoadLibraryA(dllName);

      if (!library) {
        Logger::warn(str::format("Vulkan: Failed to load ", dllName));
        continue;
      }

      auto proc = GetProcAddress(library, "vkGetInstanceProcAddr");

      if (!proc) {
        Logger::warn(str::format("Vulkan: vkGetInstanceProcAddr not found in ", dllName));
        FreeLibrary(library);
        continue;
      }

      Logger::info(str::format("Vulkan: Found vkGetInstanceProcAddr in ", dllName, " @ 0x", std::hex, reinterpret_cast<uintptr_t>(proc)));
      return std::make_pair(library, reinterpret_cast<PFN_vkGetInstanceProcAddr>(proc));
    }

    Logger::err("Vulkan: vkGetInstanceProcAddr not found");
    return { };
#endif
  }

  LibraryLoader::LibraryLoader() {
    std::tie(m_library, m_getInstanceProcAddr) = loadVulkanLibrary();
  }

  LibraryLoader::LibraryLoader(PFN_vkGetInstanceProcAddr loaderProc) {
    m_getInstanceProcAddr = loaderProc;
  }

  LibraryLoader::~LibraryLoader() {
    if (m_library)
      FreeLibrary(m_library);
  }

  PFN_vkVoidFunction LibraryLoader::sym(VkInstance instance, const char* name) const {
    return m_getInstanceProcAddr(instance, name);
  }

  PFN_vkVoidFunction LibraryLoader::sym(const char* name) const {
    return sym(nullptr, name);
  }

  
  InstanceLoader::InstanceLoader(const Rc<LibraryLoader>& library, bool owned, VkInstance instance)
  : m_library(library), m_instance(instance), m_owned(owned) { }
  
  
  PFN_vkVoidFunction InstanceLoader::sym(const char* name) const {
    return m_library->sym(m_instance, name);
  }
  
  
  DeviceLoader::DeviceLoader(const Rc<InstanceLoader>& library, bool owned, VkDevice device)
  : m_library(library)
  , m_getDeviceProcAddr(reinterpret_cast<PFN_vkGetDeviceProcAddr>(
      m_library->sym("vkGetDeviceProcAddr"))),
    m_device(device), m_owned(owned) { }
  
  
  PFN_vkVoidFunction DeviceLoader::sym(const char* name) const {
    return m_getDeviceProcAddr(m_device, name);
  }
  
  
  LibraryFn::LibraryFn() { }
  LibraryFn::LibraryFn(PFN_vkGetInstanceProcAddr loaderProc)
  : LibraryLoader(loaderProc) { }
  LibraryFn::~LibraryFn() { }
  
  
  InstanceFn::InstanceFn(const Rc<LibraryLoader>& library, bool owned, VkInstance instance)
  : InstanceLoader(library, owned, instance) { }
  InstanceFn::~InstanceFn() {
    if (m_owned)
      this->vkDestroyInstance(m_instance, nullptr);
  }
  
  
  DeviceFn::DeviceFn(const Rc<InstanceLoader>& library, bool owned, VkDevice device)
  : DeviceLoader(library, owned, device) { }
  DeviceFn::~DeviceFn() {
    if (m_owned)
      this->vkDestroyDevice(m_device, nullptr);
  }
  
}
