#include "CommandContext.h"

#include <directx/d3dx12_barriers.h>
#if USE_PIX
#include <WinPixEventRuntime/pix3.h>

void CommandContext::InsertMarker(const char* name)
{
    PIXSetMarker(this->command_list.Get(), PIX_COLOR_INDEX(0), "%s", name);
}

void CommandContext::BeginEvent(const char* name)
{
    PIXBeginEvent(this->command_list.Get(), PIX_COLOR_INDEX(0), "%s", name);
}

void CommandContext::EndEvent()
{
    PIXEndEvent(this->command_list.Get());
}
#endif

void CommandContext::Init(ID3D12GraphicsCommandList4* command_list, CbvSrvUavStack* transient_descriptors, CpuMappedLinearBuffer* transient_allocator, std::vector<D3D12_RESOURCE_BARRIER>* barriers)
{
    this->command_list = command_list;
    this->transient_descriptors = transient_descriptors;
    this->transient_allocator = transient_allocator;
    this->barriers = barriers;
}

void CommandContext::PushTransitionBarrier(ID3D12Resource* resource, D3D12_RESOURCE_STATES before_state, D3D12_RESOURCE_STATES after_state, uint32_t subresource, D3D12_RESOURCE_BARRIER_FLAGS flags)
{
    this->barriers->push_back(CD3DX12_RESOURCE_BARRIER::Transition(resource, before_state, after_state, subresource, flags));
}

void CommandContext::PushUavBarrier(ID3D12Resource* resource)
{
    this->barriers->push_back(CD3DX12_RESOURCE_BARRIER::UAV(resource));
}

void CommandContext::SubmitBarriers()
{
    this->command_list->ResourceBarrier(barriers->size(), barriers->data());
    barriers->resize(0);
}

void CommandContext::CopyTextureRegion(const D3D12_TEXTURE_COPY_LOCATION* destination, const D3D12_TEXTURE_COPY_LOCATION* source)
{
    this->command_list->CopyTextureRegion(destination, 0, 0, 0, source, nullptr);
}

void CommandContext::SetPipelineState(ID3D12PipelineState* pipeline_state)
{
    this->command_list->SetPipelineState(pipeline_state);
}

void CommandContext::SetComputeRootSignature(ID3D12RootSignature* root_signature)
{
    this->command_list->SetComputeRootSignature(root_signature);
}

void CommandContext::SetComputeRootConstantBufferView(uint32_t index, D3D12_GPU_VIRTUAL_ADDRESS gpu_address)
{
    this->command_list->SetComputeRootConstantBufferView(index, gpu_address);
}

void CommandContext::SetComputeRootShaderResourceView(uint32_t index, D3D12_GPU_VIRTUAL_ADDRESS gpu_address)
{
    this->command_list->SetComputeRootShaderResourceView(index, gpu_address);
}

void CommandContext::SetComputeRootUnorderedAccessView(uint32_t index, D3D12_GPU_VIRTUAL_ADDRESS gpu_address)
{
    this->command_list->SetComputeRootUnorderedAccessView(index, gpu_address);
}

void CommandContext::Dispatch(uint32_t thread_groups_x, uint32_t thread_groups_y, uint32_t thread_groups_z)
{
    this->command_list->Dispatch(thread_groups_x, thread_groups_y, thread_groups_z);
}

void CommandContext::SetGraphicsRootSignature(ID3D12RootSignature* root_signature)
{
    this->command_list->SetGraphicsRootSignature(root_signature);
}

void CommandContext::SetGraphicsRootDescriptorTable(uint32_t index, D3D12_GPU_DESCRIPTOR_HANDLE descriptor_start)
{
    this->command_list->SetGraphicsRootDescriptorTable(index, descriptor_start);
}

void CommandContext::SetGraphicsRootConstantBufferView(uint32_t index, D3D12_GPU_VIRTUAL_ADDRESS gpu_address)
{
    this->command_list->SetGraphicsRootConstantBufferView(index, gpu_address);
}

void CommandContext::SetGraphicsRootShaderResourceView(uint32_t index, D3D12_GPU_VIRTUAL_ADDRESS gpu_address)
{
    this->command_list->SetGraphicsRootShaderResourceView(index, gpu_address);
}

void CommandContext::SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY primitive_topology)
{
    this->command_list->IASetPrimitiveTopology(primitive_topology);
}

void CommandContext::SetIndexBuffer(const D3D12_INDEX_BUFFER_VIEW* view)
{
    this->command_list->IASetIndexBuffer(view);
}

void CommandContext::SetVertexBuffers(uint32_t start_slot, uint32_t view_count, const D3D12_VERTEX_BUFFER_VIEW* views)
{
    this->command_list->IASetVertexBuffers(start_slot, view_count, views);
}

void CommandContext::SetViewports(uint32_t count, const D3D12_VIEWPORT* viewports)
{
    this->command_list->RSSetViewports(count, viewports);
}

void CommandContext::SetScissorRects(uint32_t count, const D3D12_RECT* rects)
{
    this->command_list->RSSetScissorRects(count, rects);
}

void CommandContext::ClearRenderTargetView(D3D12_CPU_DESCRIPTOR_HANDLE render_target_view, const float* color)
{
    this->command_list->ClearRenderTargetView(render_target_view, color, 0, nullptr);
}

void CommandContext::ClearDepthStencilView(D3D12_CPU_DESCRIPTOR_HANDLE depth_stencil_view, float depth)
{
    this->command_list->ClearDepthStencilView(depth_stencil_view, D3D12_CLEAR_FLAG_DEPTH, depth, 0, 0, nullptr);
}

void CommandContext::SetRenderTargets(uint32_t render_target_count, const D3D12_CPU_DESCRIPTOR_HANDLE* render_targets, const D3D12_CPU_DESCRIPTOR_HANDLE* depth_stencil)
{
    this->command_list->OMSetRenderTargets(render_target_count, render_targets, false, depth_stencil);
}

void CommandContext::Draw(uint32_t vertex_count, uint32_t vertex_start_location)
{
    this->command_list->DrawInstanced(vertex_count, 1, vertex_start_location, 0);
}

void CommandContext::DrawIndexed(uint32_t index_count, uint32_t index_start_location, int base_vertex_location)
{
    this->command_list->DrawIndexedInstanced(index_count, 1, index_start_location, base_vertex_location, 0);
}

void CommandContext::DrawInstanced(uint32_t vertex_count_per_instance, uint32_t instance_count, uint32_t vertex_start_location, uint32_t instance_start_location)
{
    this->command_list->DrawInstanced(vertex_count_per_instance, instance_count, vertex_start_location, instance_start_location);
}

void CommandContext::DrawIndexedInstanced(uint32_t index_count_per_instance, uint32_t instance_count, uint32_t index_start_location, int base_vertex_location, uint32_t instance_start_location)
{
    this->command_list->DrawIndexedInstanced(index_count_per_instance, instance_count, index_start_location, base_vertex_location, instance_start_location);
}

void CommandContext::BuildRaytracingAccelerationStructure(const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC* desc, uint32_t postbuild_info_count, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_DESC* postbuild_info)
{
    this->command_list->BuildRaytracingAccelerationStructure(desc, postbuild_info_count, postbuild_info);
}

void CommandContext::SetPipelineState(ID3D12StateObject* state_object)
{
    this->command_list->SetPipelineState1(state_object);
}

void CommandContext::DispatchRays(const D3D12_DISPATCH_RAYS_DESC* desc)
{
    this->command_list->DispatchRays(desc);
}

void* CommandContext::Allocate(uint64_t size, uint64_t alignment, D3D12_GPU_VIRTUAL_ADDRESS* gpu_address)
{
    return this->transient_allocator->Allocate(size, alignment, gpu_address);
}

D3D12_GPU_VIRTUAL_ADDRESS CommandContext::AllocateAndCopy(const void* data, uint64_t size, uint64_t alignment)
{
    return this->transient_allocator->Copy(data, size, alignment);
}

D3D12_GPU_VIRTUAL_ADDRESS CommandContext::CreateConstantBuffer(const void* constant_buffer, uint64_t size)
{
    return this->transient_allocator->Copy(constant_buffer, size, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
}

DescriptorSpan CommandContext::AllocateDescriptors(int count)
{
    int descriptor_start = this->transient_descriptors->Allocate(count);
    if (descriptor_start == -1) {
        return DescriptorSpan();
    }
    return DescriptorSpan(descriptor_start, count);
}

void CommandContext::CreateSrv(int index, ID3D12Resource* resource, const D3D12_SHADER_RESOURCE_VIEW_DESC* srv_desc)
{
    transient_descriptors->CreateSrv(index, resource, srv_desc);
}

void CommandContext::CreateUav(int index, ID3D12Resource* resource, ID3D12Resource* counter_resource, const D3D12_UNORDERED_ACCESS_VIEW_DESC* uav_desc)
{
    transient_descriptors->CreateUav(index, resource, counter_resource, uav_desc);
}
