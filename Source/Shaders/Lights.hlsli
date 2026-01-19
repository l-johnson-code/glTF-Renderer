#pragma once

enum LightType {
    LIGHT_TYPE_POINT,
    LIGHT_TYPE_SPOT,
    LIGHT_TYPE_DIRECTIONAL,
};

struct Light {
    int type;
    float3 position;
    float cutoff;
    float3 direction;
    float intensity;
    float3 color;
    float inner_angle;
    float outer_angle;
	float2 pad;
};

struct LightRay {
    float3 direction;
    float3 color;
};

LightRay GetLightRay(Light light, float3 surface_world_pos)
{
    LightRay ray;
	if (light.type == LIGHT_TYPE_POINT || light.type == LIGHT_TYPE_SPOT) {
		ray.direction = light.position - surface_world_pos;
	} else {
		ray.direction = -light.direction;
	}
	ray.color = light.color * light.intensity;

	// Calculate distance falloff for point and spotlights.
	if (light.type == LIGHT_TYPE_POINT || light.type == LIGHT_TYPE_SPOT) {
		float distance = length(ray.direction);
		float falloff = 1.0;
		if (light.cutoff > 0.0f) {
			falloff = max(min(1.0 - pow(distance / light.cutoff, 4.0), 1.0), 0.0);
		}
		falloff /= distance * distance;
		ray.color *= falloff;
	}

	ray.direction = normalize(ray.direction);

	// Calculate angular falloff for spotlights.
	if (light.type == LIGHT_TYPE_SPOT) {
		// TODO: These first two values can be calculated on the CPU and passed into the shader.
		float light_angle_scale = 1.0f / max(0.001f, cos(light.inner_angle) - cos(light.outer_angle));
		float light_angle_offset = -cos(light.outer_angle) * light_angle_scale;
		float cd = -dot(normalize(light.direction), ray.direction);
		float angular_attenuation = saturate(cd * light_angle_scale + light_angle_offset);
		angular_attenuation *= angular_attenuation;
		ray.color *= angular_attenuation;
	}

	return ray;
}