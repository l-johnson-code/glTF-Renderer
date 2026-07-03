#include "DebugDraw.h"

Gpu::Resources* DebugDraw::resources = nullptr;
MultiBuffer<Gpu::Buffer, Config::FRAME_COUNT> DebugDraw::gpu_vertices;
Microsoft::WRL::ComPtr<ID3D12PipelineState> DebugDraw::pipeline;
int DebugDraw::max_vertices = 0;
std::vector<DebugDraw::Vertex> DebugDraw::vertices;

HRESULT DebugDraw::Init(Gpu::Resources* resources, int max_vertices)
{
    DebugDraw::resources = resources;

    // Create the pipeline.
    D3D12_INPUT_ELEMENT_DESC input_elements[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };
    Gpu::GraphicsPipelineDesc pipeline_desc = {
        .name = "Debug Lines",
        .vertex_shader = "DebugLine",
        .pixel_shader = "DebugLine",
        .input_layout = {
            .pInputElementDescs = input_elements,
            .NumElements = std::size(input_elements),
        },
        .primitive_topology_type = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE,
        .render_target_count = 1,
        .render_target_formats = {
            DXGI_FORMAT_R8G8B8A8_UNORM,
        },
    };
    HRESULT result = resources->CreateGraphicsPipelineState(&pipeline_desc, &DebugDraw::pipeline);
    if (FAILED(result)) {
        Shutdown();
        return result;
    }

    // Create the GPU buffers.
    for (int i = 0; i < DebugDraw::gpu_vertices.Size(); i++) {
        Gpu::BufferDesc buffer_desc = {
            .name = "Debug Lines",
            .size = max_vertices * sizeof(GpuVertex),
            .flags = Gpu::BUFFER_FLAG_PERSISTENT_MAP,
            .heap_type = D3D12_HEAP_TYPE_UPLOAD,
        };
        result = DebugDraw::resources->CreateBuffer(&buffer_desc, &DebugDraw::gpu_vertices[i]);
        if (FAILED(result)) {
            Shutdown();
            return result;
        }
    }
    DebugDraw::max_vertices = max_vertices;

    return result;
}

void DebugDraw::DrawLine(glm::vec3 start, glm::vec3 end, glm::vec3 color, float duration)
{
    Vertex vertex = {
        .gpu_vertex = {
            .position = start,
            .color = glm::packUnorm4x8(glm::vec4(color, 1.0f)),
        },
        .duration = duration,
    };
    DebugDraw::vertices.push_back(vertex);
    vertex.gpu_vertex.position = end;
    DebugDraw::vertices.push_back(vertex);
}

void DebugDraw::DrawTriangle(glm::vec3 vertex_1, glm::vec3 vertex_2, glm::vec3 vertex_3, glm::vec3 color, float duration)
{
    DrawLine(vertex_1, vertex_2, color, duration);
    DrawLine(vertex_2, vertex_3, color, duration);
    DrawLine(vertex_3, vertex_1, color, duration);
}

void DebugDraw::DrawPoint(glm::vec3 position, glm::vec3 color, float duration)
{
    float point_size = 0.1f;
    DrawLine(position - glm::vec3(point_size, 0.0f, 0.0f), position + glm::vec3(point_size, 0.0f, 0.0f), color, duration);
    DrawLine(position - glm::vec3(0.0f, point_size, 0.0f), position + glm::vec3(0.0f, point_size, 0.0f), color, duration);
    DrawLine(position - glm::vec3(0.0f, 0.0f, point_size), position + glm::vec3(0.0f, 0.0f, point_size), color, duration);
}

void DebugDraw::DrawBox(glm::vec3 min, glm::vec3 max, glm::vec3 color, float duration)
{
    glm::vec3 vertices[8] = {
        glm::vec3(min.x, min.y, min.z),
        glm::vec3(max.x, min.y, min.z),
        glm::vec3(min.x, max.y, min.z),
        glm::vec3(max.x, max.y, min.z),
        glm::vec3(min.x, min.y, max.z),
        glm::vec3(max.x, min.y, max.z),
        glm::vec3(min.x, max.y, max.z),
        glm::vec3(max.x, max.y, max.z),
    };
    DrawLine(vertices[0], vertices[1], color, duration);
    DrawLine(vertices[2], vertices[3], color, duration);
    DrawLine(vertices[0], vertices[2], color, duration);
    DrawLine(vertices[1], vertices[3], color, duration);
    DrawLine(vertices[0], vertices[4], color, duration);
    DrawLine(vertices[1], vertices[5], color, duration);
    DrawLine(vertices[2], vertices[6], color, duration);
    DrawLine(vertices[3], vertices[7], color, duration);
    DrawLine(vertices[4], vertices[5], color, duration);
    DrawLine(vertices[6], vertices[7], color, duration);
    DrawLine(vertices[4], vertices[6], color, duration);
    DrawLine(vertices[5], vertices[7], color, duration);
}

void DebugDraw::DrawBox(glm::vec3 min, glm::vec3 max, glm::mat4x4 transform, glm::vec3 color, float duration)
{
    glm::vec3 vertices[8] = {
        transform * glm::vec4(min.x, min.y, min.z, 1.0f),
        transform * glm::vec4(max.x, min.y, min.z, 1.0f),
        transform * glm::vec4(min.x, max.y, min.z, 1.0f),
        transform * glm::vec4(max.x, max.y, min.z, 1.0f),
        transform * glm::vec4(min.x, min.y, max.z, 1.0f),
        transform * glm::vec4(max.x, min.y, max.z, 1.0f),
        transform * glm::vec4(min.x, max.y, max.z, 1.0f),
        transform * glm::vec4(max.x, max.y, max.z, 1.0f),
    };
    DrawLine(vertices[0], vertices[1], color, duration);
    DrawLine(vertices[2], vertices[3], color, duration);
    DrawLine(vertices[0], vertices[2], color, duration);
    DrawLine(vertices[1], vertices[3], color, duration);
    DrawLine(vertices[0], vertices[4], color, duration);
    DrawLine(vertices[1], vertices[5], color, duration);
    DrawLine(vertices[2], vertices[6], color, duration);
    DrawLine(vertices[3], vertices[7], color, duration);
    DrawLine(vertices[4], vertices[5], color, duration);
    DrawLine(vertices[6], vertices[7], color, duration);
    DrawLine(vertices[4], vertices[6], color, duration);
    DrawLine(vertices[5], vertices[7], color, duration);
}

void DebugDraw::Render(CommandContext* context, const glm::mat4x4* world_to_clip)
{
    // Copy vertices to GPU.
    GpuVertex* dest = (GpuVertex*)DebugDraw::gpu_vertices.Current().Pointer();
    int vertex_count = std::min((size_t)max_vertices, vertices.size());
    for (int i = 0; i < vertex_count; i++) {
        dest[i] = vertices[i].gpu_vertex;
    }

    // Draw vertices.
    context->SetPipelineState(DebugDraw::pipeline.Get());
    context->SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
    context->SetGraphicsRootConstantBufferView(Gpu::GENERIC_GRAPHICS_ROOT_PARAMETER_CONSTANT_BUFFER_VERTEX_PER_FRAME, world_to_clip);
    D3D12_VERTEX_BUFFER_VIEW vertex_buffer_view = {
        .BufferLocation = DebugDraw::gpu_vertices.Current().Resource()->GetGPUVirtualAddress(),
        .SizeInBytes = vertex_count * (uint32_t)sizeof(GpuVertex),
        .StrideInBytes = sizeof(GpuVertex),
    };
    context->SetVertexBuffers(0, 1, &vertex_buffer_view);
    context->Draw(vertex_count);

    DebugDraw::gpu_vertices.Next();
}

void DebugDraw::RemoveExpiredVertices(float delta_time)
{
    size_t new_size = 0;
    for (int i = 0; i < vertices.size(); i++) {
        vertices[i].duration -= delta_time;
        if (vertices[i].duration > 0.0f) {
            vertices[new_size] = vertices[i];
            new_size++;
        }
    }
    vertices.resize(new_size);
}

void DebugDraw::Shutdown()
{
    if (DebugDraw::resources) {
        for (int i = 0; i < DebugDraw::gpu_vertices.Size(); i++) {
            DebugDraw::resources->FreeBuffer(&DebugDraw::gpu_vertices[i]);
        }
    }
}
