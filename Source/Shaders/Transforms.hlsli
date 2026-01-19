#pragma once

float2 DirectionToEquirectangular(float3 direction)
{
    const float TAU = 6.28318530717;
    float2 equirectangular_coordinates = float2(atan2(direction.y, direction.x) / TAU, 1 - ((direction.z + 1) / 2));
    return equirectangular_coordinates;
}

float3 CubemapToDirection(int face, float2 uv)
{
    float3 u_direction;
    float3 v_direction;
    float3 face_direction;
    switch (face) {
        case 0: {
            face_direction = float3(1, 0, 0);
            u_direction = float3(0, 0, -1);
            v_direction = float3(0, -1, 0);
        } break;
        case 1: {
            face_direction = float3(-1, 0, 0);
            u_direction = float3(0, 0, 1);
            v_direction = float3(0, -1, 0);
        } break;
        case 2: {
            face_direction = float3(0, 1, 0);
            u_direction = float3(1, 0, 0);
            v_direction = float3(0, 0, 1);
        } break;
        case 3: {
            face_direction = float3(0, -1, 0);
            u_direction = float3(1, 0, 0);
            v_direction = float3(0, 0, -1);
        } break;
        case 4: {
            face_direction = float3(0, 0, 1);
            u_direction = float3(1, 0, 0);
            v_direction = float3(0, -1, 0);
        } break;
        case 5: {
            face_direction = float3(0, 0, -1);
            u_direction = float3(-1, 0, 0);
            v_direction = float3(0, -1, 0);
        } break;
    }
    uv = uv * 2 - 1;
    float3 direction = normalize(face_direction + uv.x * u_direction + uv.y * v_direction);
    return direction;
}

float2 UvToUnitSquare(float2 uv)
{
    return uv * float2(2, -2) + float2(-1, 1);
}

float2 UnitSquareToUv(float2 square)
{
    return (square - float2(-1, 1)) * float2(0.5, -0.5);
}

// Maps the unit square to the unit disk using a concentric mapping.
float2 SquareToDisk(float2 square)
{
    const float PI = 3.14159265359;
    float phi;
    float r;
    if (square.x == 0 && square.y == 0) {
        return 0.xx;
    };
    if (abs(square.x) > abs(square.y)) {
        r = square.x;
        phi = (PI / 4) * (square.y / square.x);
    } else {
        r = square.y;
        phi = (PI / 2) - (PI / 4) * (square.x / square.y);
    }
    return r * float2(cos(phi), sin(phi));
}

// Branchless implementation of the same function.
float2 SquareToDisk2(float2 square)
{
    const float PI = 3.14159265359;
    float r = max(abs(square.x), abs(square.y));
    float phi = r == 0 ? 0 : (PI * (r + (abs(square.y) - abs(square.x))) / (4 * r));
    float2 disk = float2(sign(square.x) * r * cos(phi), sign(square.y) * r * sin(phi));
    return disk;
}

// Maps the unit disk to the unit square using a concentric mapping.
float2 DiskToSquare(float2 disk)
{
    const float PI = 3.14159265359;
    float r = length(disk);
    float phi = atan2(disk.y, disk.x);
    phi = phi >= (-PI / 4) ?  phi + 2 * PI : phi;
    float2 square;
    if (phi < (PI / 4)) {
        square = float2(r, (4 * r * phi) / PI);
    } else if (phi < (3 * PI / 4)) {
        square = float2((-4 * r * (phi - PI/2)) / PI, r);
    } else if (phi < (5 * PI / 4)) {
        square = float2(-r, (-4 * r * (phi - PI)) / PI);
    } else {
        square = float2((4 * r * (phi - 3*PI/2)) / PI, -r);
    }
    return square;
}

// Branchless implementation of the same function.
float2 DiskToSquare2(float2 disk)
{
    const float PI = 3.14159265359;
    float r = length(disk);
    float phi = atan2(abs(disk.y), abs(disk.x));
    float temp = (4 / PI) * phi;
    float2 square;
    square.x = sign(disk.x) * r * (1 - saturate(phi - 1));
    square.y = sign(disk.y) * r * saturate(phi);
    return square;
}

// Maps the unit square to the unit sphere through a combination of concentric and octahedral mapping.
float3 SquareToSphere(float2 square)
{
    const float PI = 3.14159265359;
    float d = 1 - (abs(square.x) + abs(square.y));
    float r = 1 - abs(d);
    float phi = (r == 0) ? 0 : (PI / 4) * ((abs(square.y) - abs(square.x)) / r + 1);
    float f = r * sqrt(2 - r * r);
    float3 sphere;
    sphere.x = f * sign(square.x) * cos(phi);
    sphere.y = f * sign(square.y) * sin(phi);
    sphere.z = sign(d) * (1 - r * r);
    return sphere;
}

float2 SphereToSquare(float3 sphere)
{
    const float PI = 3.14159265359;
    float r = sqrt(1 - abs(sphere.z));
    float phi = atan2(abs(sphere.y), abs(sphere.x));
    float d = sign(sphere.z) * (1 - r);
    float diff = r * ((4 / PI) * phi - 1);
    float2 square;
    square.x = sign(sphere.x) * 0.5 * (1 - d - diff);
    square.y = sign(sphere.y) * 0.5 * (1 - d + diff);
    return square;
}