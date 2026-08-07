#pragma once


#include "CommandContext.h"
#include "GpuResources.h"

class DebugDraw {

    public:

    static HRESULT Init(Gpu::Resources* resources, int max_vertices);
    static void DrawLine(glm::vec3 start, glm::vec3 end, glm::vec3 color, float duration);
    static void DrawTriangle(glm::vec3 vertex_1, glm::vec3 vertex_2, glm::vec3 vertex_3, glm::vec3 color, float duration);
    static void DrawPoint(glm::vec3 position, glm::vec3 color, float duration);
    static void DrawBox(glm::vec3 min, glm::vec3 max, glm::vec3 color, float duration);
    static void DrawBox(glm::vec3 min, glm::vec3 max, glm::mat4x4 transform, glm::vec3 color, float duration);
    static void Render(CommandContext* context, const glm::mat4x4* world_to_clip);
    static void RemoveExpiredVertices(float delta_time);
    static void Shutdown();

    private:

    struct GpuVertex {
        glm::vec3 position;
        uint32_t color;
    };

    struct Vertex {
        GpuVertex gpu_vertex;
        float duration;
    };

    static Gpu::Resources* resources;
    static MultiBuffer<Gpu::Buffer, Config::FRAME_COUNT> gpu_vertices;
    static Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline;
    static int max_vertices;
    static std::vector<Vertex> vertices;
};