#pragma once

// Wrapper to properly handle multiple renders for ImGui across GTA V and RDR3
#define USE_SHARED_DLL

// DirectX12 Heap allocator for ImGui
#ifdef IS_RDR3
#include <boost/circular_buffer.hpp>
#include <d3d12.h>

struct D3D12HeapAllocator
{
	ID3D12DescriptorHeap* m_heap = nullptr;
	D3D12_DESCRIPTOR_HEAP_TYPE m_heapType = D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES;
	D3D12_CPU_DESCRIPTOR_HANDLE m_heapStartCpu;
	D3D12_GPU_DESCRIPTOR_HANDLE m_heapStartGpu;
	UINT m_heapHandleIncrement;
	boost::circular_buffer<int> m_freeIndices{ 4000 };

	void Create(ID3D12Device* device, ID3D12DescriptorHeap* heap)
	{
		m_heap = heap;
		D3D12_DESCRIPTOR_HEAP_DESC desc = heap->GetDesc();
		m_heapType = desc.Type;
		m_heapStartCpu = m_heap->GetCPUDescriptorHandleForHeapStart();
		m_heapStartGpu = m_heap->GetGPUDescriptorHandleForHeapStart();
		m_heapHandleIncrement = device->GetDescriptorHandleIncrementSize(m_heapType);
		m_freeIndices.set_capacity(desc.NumDescriptors);
		for (int n = desc.NumDescriptors; n > 0; n--)
		{
			m_freeIndices.push_back(n - 1);
		}
	}

	void Destroy()
	{
		m_heap = nullptr;
		m_freeIndices.clear();
	}

	void Alloc(D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu_desc_handle, D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu_desc_handle)
	{
		int idx = m_freeIndices.back();
		m_freeIndices.pop_back();
		out_cpu_desc_handle->ptr = m_heapStartCpu.ptr + (idx * m_heapHandleIncrement);
		out_gpu_desc_handle->ptr = m_heapStartGpu.ptr + (idx * m_heapHandleIncrement);
	}

	void Free(D3D12_CPU_DESCRIPTOR_HANDLE out_cpu_desc_handle, D3D12_GPU_DESCRIPTOR_HANDLE out_gpu_desc_handle)
	{
		int cpu_idx = (int)((out_cpu_desc_handle.ptr - m_heapStartCpu.ptr) / m_heapHandleIncrement);
		int gpu_idx = (int)((out_gpu_desc_handle.ptr - m_heapStartGpu.ptr) / m_heapHandleIncrement);
		m_freeIndices.push_back(cpu_idx);
	}
};

#endif

namespace ConHost
{
#ifdef USE_SHARED_DLL
	// For actions that are needed early for shared dll.
	void SetupSharedDevice();
#endif
	// For ImGUI initalization for Win32 render and the games specific DX/Vulkan runtime
	void InitPlatform();

	// For new ImGUI frame for Win32, ImGui and the games specific DX/Vulkan runtime
	void PlatformNewFrame();

	// For rendering ImGUI data for Win32, ImGui, the games specific DX/Vulkan runtime
	void PlatformRender();

	// Special case for rendering textures that were created with grcTexture
	void RenderGrcImage(void*, void*);
}
