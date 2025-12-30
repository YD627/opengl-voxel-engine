#define NOMINMAX
#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <iostream>
#include <vector>
#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <fstream> 
#include <sstream> 

// ==========================================
// 全局设置
// ==========================================
unsigned int SCR_WIDTH = 1024;
unsigned int SCR_HEIGHT = 768;
const unsigned int SHADOW_WIDTH = 4096, SHADOW_HEIGHT = 4096;

// 相机
glm::vec3 cameraPos = glm::vec3(0.0f);
glm::vec3 cameraFront = glm::vec3(0.0f, 0.0f, -1.0f);
glm::vec3 cameraUp = glm::vec3(0.0f, 1.0f, 0.0f);

// 鼠标
bool firstMouse = true;
float yaw = -90.0f;
float pitch = 0.0f;
float lastX = SCR_WIDTH / 2.0;
float lastY = SCR_HEIGHT / 2.0;
float fov = 60.0f;

float deltaTime = 0.0f;
float lastFrame = 0.0f;

// 游戏模式
enum CameraMode { FIRST_PERSON = 0, THIRD_PERSON = 1, FREE_CAM = 2 };
int currentCamMode = FIRST_PERSON;
bool vKeyPressed = false;

// 机器人
glm::vec3 robotPos(0.0f, 5.0f, 2.0f);
// 视觉偏移
glm::vec3 visualOffset(0.0f, 0.375f, 0.0f);

float robotRot = 0.0f;
float armAngle = 0.0f; bool armDir = true;

// 物理
float verticalVelocity = 0.0f;
float gravity = 20.0f;
float jumpStrength = 8.0f;
bool isGrounded = false;

// 光照
glm::vec3 lightPos(0.0f, 50.0f, 0.0f);

// 交互
struct Block; bool hasSelection = false; glm::vec3 selectedBlockPos(0.0f);

// ==========================================
// 数据结构
// ==========================================
enum BlockType { GRASS_BLOCK, DIRT, STONE, COAL_ORE, IRON_ORE };
struct Block { glm::vec3 position; BlockType type; };

std::vector<Block> terrainBlocks;
std::vector<glm::vec3> trunkBlocks;
std::vector<glm::vec3> leafBlocks;

// 箱子 (交互物体)
glm::vec3 chestPos;
float chestRotation = 0.0f;

// 静态碰撞体
std::vector<glm::vec3> collisionBlocks;

unsigned int charTexture, grassSideTex, grassTopTex, dirtTex, stoneTex, coalTex, ironTex, logTex, logTopTex, leafTex;
unsigned int chestFrontTex, chestSideTex, chestTopTex;
unsigned int swordTexture;

unsigned int headVAO, torsoVAO, limbVAO, legVAO, blockVAO;
unsigned int headVBO, torsoVBO, limbVBO, legVBO, blockVBO;

// 剑的 VAO/VBO
unsigned int swordVAO, swordVBO;

unsigned int depthMapFBO, depthMapTex;

// ==========================================
// 阴影 Shader
// ==========================================
const char* simpleDepthVertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 2) in vec2 aTexCoord;
out vec2 TexCoord;
uniform mat4 lightSpaceMatrix;
uniform mat4 model;
void main() {
    TexCoord = aTexCoord;
    gl_Position = lightSpaceMatrix * model * vec4(aPos, 1.0);
}
)";

const char* simpleDepthFragmentShaderSource = R"(
#version 330 core
in vec2 TexCoord;
uniform sampler2D texture1;
uniform int hasTexture;
void main() {
    if (hasTexture == 1) {
        float alpha = texture(texture1, TexCoord).a;
        if (alpha < 0.1) discard;
    }
}
)";

// ==========================================
// 碰撞盒 AABB
// ==========================================
struct AABB { glm::vec3 min; glm::vec3 max; };

AABB getRobotAABB(glm::vec3 pos) {
    return { pos + glm::vec3(-0.3f, 0.0f, -0.3f), pos + glm::vec3(0.3f, 1.8f, 0.3f) };
}
AABB getBlockAABB(glm::vec3 pos) {
    return { pos - glm::vec3(0.5f, 0.5f, 0.5f), pos + glm::vec3(0.5f, 0.5f, 0.5f) };
}

bool checkCollision(AABB b1, AABB b2) {
    return (b1.min.x <= b2.max.x && b1.max.x >= b2.min.x) &&
        (b1.min.y <= b2.max.y && b1.max.y >= b2.min.y) &&
        (b1.min.z <= b2.max.z && b1.max.z >= b2.min.z);
}

// 场景碰撞检测 (静态 + 动态)
bool checkSceneCollision(AABB robotBox) {
    // 1. 静态方块
    for (const auto& pos : collisionBlocks) {
        if (checkCollision(robotBox, getBlockAABB(pos))) return true;
    }
    // 2. 动态箱子
    if (checkCollision(robotBox, getBlockAABB(chestPos))) return true;

    return false;
}

// ==========================================
// 射线检测
// ==========================================
bool intersectRayAABB(glm::vec3 rayOrigin, glm::vec3 rayDir, AABB box, float& distance) {
    glm::vec3 invDir = 1.0f / rayDir;
    glm::vec3 tMin = (box.min - rayOrigin) * invDir;
    glm::vec3 tMax = (box.max - rayOrigin) * invDir;
    glm::vec3 t1 = glm::min(tMin, tMax);
    glm::vec3 t2 = glm::max(tMin, tMax);
    float tNear = std::max(std::max(t1.x, t1.y), t1.z);
    float tFar = std::min(std::min(t2.x, t2.y), t2.z);
    if (tNear > tFar || tFar < 0) return false;
    distance = tNear;
    return true;
}

// 更新方块高亮 (包含动态物体)
void updateBlockHighlight() {
    float minDst = 1000.0f; float reach = 6.0f; hasSelection = false;
    glm::vec3 eyePos = cameraPos; glm::vec3 rayDir = cameraFront;

    // 1. 静态方块
    for (const auto& pos : collisionBlocks) {
        float dst;
        if (intersectRayAABB(eyePos, rayDir, getBlockAABB(pos), dst)) {
            if (dst < minDst && dst < reach) { minDst = dst; selectedBlockPos = pos; hasSelection = true; }
        }
    }

    // 2. 动态箱子
    float dstChest;
    if (intersectRayAABB(eyePos, rayDir, getBlockAABB(chestPos), dstChest)) {
        if (dstChest < minDst && dstChest < reach) {
            minDst = dstChest;
            selectedBlockPos = chestPos;
            hasSelection = true;
        }
    }
}

// ==========================================
// 顶点数据
// ==========================================
float headVertices[] = { -0.5f,-0.5f,0.5f,0.0f,0.0f,1.0f,0.125f,0.500f,0.5f,-0.5f,0.5f,0.0f,0.0f,1.0f,0.250f,0.500f,0.5f,0.5f,0.5f,0.0f,0.0f,1.0f,0.250f,0.750f,0.5f,0.5f,0.5f,0.0f,0.0f,1.0f,0.250f,0.750f,-0.5f,0.5f,0.5f,0.0f,0.0f,1.0f,0.125f,0.750f,-0.5f,-0.5f,0.5f,0.0f,0.0f,1.0f,0.125f,0.500f,-0.5f,-0.5f,-0.5f,0.0f,0.0f,-1.0f,0.375f,0.500f,0.5f,0.5f,-0.5f,0.0f,0.0f,-1.0f,0.500f,0.750f,0.5f,-0.5f,-0.5f,0.0f,0.0f,-1.0f,0.500f,0.500f,0.5f,0.5f,-0.5f,0.0f,0.0f,-1.0f,0.500f,0.750f,-0.5f,-0.5f,-0.5f,0.0f,0.0f,-1.0f,0.375f,0.500f,-0.5f,0.5f,-0.5f,0.0f,0.0f,-1.0f,0.375f,0.750f,0.5f,0.5f,0.5f,1.0f,0.0f,0.0f,0.250f,0.750f,0.5f,-0.5f,-0.5f,1.0f,0.0f,0.0f,0.375f,0.500f,0.5f,0.5f,-0.5f,1.0f,0.0f,0.0f,0.375f,0.750f,0.5f,-0.5f,-0.5f,1.0f,0.0f,0.0f,0.375f,0.500f,0.5f,0.5f,0.5f,1.0f,0.0f,0.0f,0.250f,0.750f,0.5f,-0.5f,0.5f,1.0f,0.0f,0.0f,0.250f,0.500f,-0.5f,0.5f,0.5f,-1.0f,0.0f,0.0f,0.000f,0.750f,-0.5f,0.5f,-0.5f,-1.0f,0.0f,0.0f,0.125f,0.750f,-0.5f,-0.5f,-0.5f,-1.0f,0.0f,0.0f,0.125f,0.500f,-0.5f,-0.5f,-0.5f,-1.0f,0.0f,0.0f,0.125f,0.500f,-0.5f,-0.5f,0.5f,-1.0f,0.0f,0.0f,0.000f,0.500f,-0.5f,0.5f,0.5f,-1.0f,0.0f,0.0f,0.000f,0.750f,-0.5f,0.5f,-0.5f,0.0f,1.0f,0.0f,0.125f,0.750f,0.5f,0.5f,0.5f,0.0f,1.0f,0.0f,0.250f,1.000f,0.5f,0.5f,-0.5f,0.0f,1.0f,0.0f,0.250f,0.750f,0.5f,0.5f,0.5f,0.0f,1.0f,0.0f,0.250f,1.000f,-0.5f,0.5f,-0.5f,0.0f,1.0f,0.0f,0.125f,0.750f,-0.5f,0.5f,0.5f,0.0f,1.0f,0.0f,0.125f,1.000f,-0.5f,-0.5f,-0.5f,0.0f,-1.0f,0.0f,0.250f,0.750f,0.5f,-0.5f,-0.5f,0.0f,-1.0f,0.0f,0.375f,0.750f,0.5f,-0.5f,0.5f,0.0f,-1.0f,0.0f,0.375f,1.000f,0.5f,-0.5f,0.5f,0.0f,-1.0f,0.0f,0.375f,1.000f,-0.5f,-0.5f,0.5f,0.0f,-1.0f,0.0f,0.250f,1.000f,-0.5f,-0.5f,-0.5f,0.0f,-1.0f,0.0f,0.250f,0.750f };
float torsoVertices[] = { -0.5f,-0.5f,0.5f,0.0f,0.0f,1.0f,0.3125f,0.000f,0.5f,-0.5f,0.5f,0.0f,0.0f,1.0f,0.4375f,0.000f,0.5f,0.5f,0.5f,0.0f,0.0f,1.0f,0.4375f,0.375f,0.5f,0.5f,0.5f,0.0f,0.0f,1.0f,0.4375f,0.375f,-0.5f,0.5f,0.5f,0.0f,0.0f,1.0f,0.3125f,0.375f,-0.5f,-0.5f,0.5f,0.0f,0.0f,1.0f,0.3125f,0.000f,-0.5f,-0.5f,-0.5f,0.0f,0.0f,-1.0f,0.5000f,0.000f,0.5f,0.5f,-0.5f,0.0f,0.0f,-1.0f,0.6250f,0.375f,0.5f,-0.5f,-0.5f,0.0f,0.0f,-1.0f,0.6250f,0.000f,0.5f,0.5f,-0.5f,0.0f,0.0f,-1.0f,0.6250f,0.375f,-0.5f,-0.5f,-0.5f,0.0f,0.0f,-1.0f,0.5000f,0.000f,-0.5f,0.5f,-0.5f,0.0f,0.0f,-1.0f,0.5000f,0.375f,-0.5f,0.5f,-0.5f,0.0f,1.0f,0.0f,0.3125f,0.375f,0.5f,0.5f,0.5f,0.0f,1.0f,0.0f,0.4375f,0.500f,0.5f,0.5f,-0.5f,0.0f,1.0f,0.0f,0.4375f,0.375f,0.5f,0.5f,0.5f,0.0f,1.0f,0.0f,0.4375f,0.500f,-0.5f,0.5f,-0.5f,0.0f,1.0f,0.0f,0.3125f,0.375f,-0.5f,0.5f,0.5f,0.0f,1.0f,0.0f,0.3125f,0.500f,-0.5f,-0.5f,-0.5f,0.0f,-1.0f,0.0f,0.4375f,0.375f,0.5f,-0.5f,-0.5f,0.0f,-1.0f,0.0f,0.5625f,0.375f,0.5f,-0.5f,0.5f,0.0f,-1.0f,0.0f,0.5625f,0.500f,0.5f,-0.5f,0.5f,0.0f,-1.0f,0.0f,0.5625f,0.500f,-0.5f,-0.5f,0.5f,0.0f,-1.0f,0.0f,0.4375f,0.500f,-0.5f,-0.5f,-0.5f,0.0f,-1.0f,0.0f,0.4375f,0.375f,0.5f,0.5f,0.5f,1.0f,0.0f,0.0f,0.2500f,0.375f,0.5f,-0.5f,-0.5f,1.0f,0.0f,0.0f,0.3125f,0.000f,0.5f,0.5f,-0.5f,1.0f,0.0f,0.0f,0.3125f,0.375f,0.5f,-0.5f,-0.5f,1.0f,0.0f,0.0f,0.3125f,0.000f,0.5f,0.5f,0.5f,1.0f,0.0f,0.0f,0.2500f,0.375f,0.5f,-0.5f,0.5f,1.0f,0.0f,0.0f,0.2500f,0.000f,-0.5f,0.5f,0.5f,-1.0f,0.0f,0.0f,0.4375f,0.375f,-0.5f,0.5f,-0.5f,-1.0f,0.0f,0.0f,0.5000f,0.375f,-0.5f,-0.5f,-0.5f,-1.0f,0.0f,0.0f,0.5000f,0.000f,-0.5f,-0.5f,-0.5f,-1.0f,0.0f,0.0f,0.5000f,0.000f,-0.5f,-0.5f,0.5f,-1.0f,0.0f,0.0f,0.4375f,0.000f,-0.5f,0.5f,0.5f,-1.0f,0.0f,0.0f,0.4375f,0.375f };
float limbVertices[] = { -0.5f,-0.5f,0.5f,0.0f,0.0f,1.0f,0.6875f,0.000f,0.5f,-0.5f,0.5f,0.0f,0.0f,1.0f,0.7500f,0.000f,0.5f,0.5f,0.5f,0.0f,0.0f,1.0f,0.7500f,0.375f,0.5f,0.5f,0.5f,0.0f,0.0f,1.0f,0.7500f,0.375f,-0.5f,0.5f,0.5f,0.0f,0.0f,1.0f,0.6875f,0.375f,-0.5f,-0.5f,0.5f,0.0f,0.0f,1.0f,0.6875f,0.000f,-0.5f,-0.5f,-0.5f,0.0f,0.0f,-1.0f,0.8125f,0.000f,0.5f,0.5f,-0.5f,0.0f,0.0f,-1.0f,0.8750f,0.375f,0.5f,-0.5f,-0.5f,0.0f,0.0f,-1.0f,0.8750f,0.000f,0.5f,0.5f,-0.5f,0.0f,0.0f,-1.0f,0.8750f,0.375f,-0.5f,-0.5f,-0.5f,0.0f,0.0f,-1.0f,0.8125f,0.000f,-0.5f,0.5f,-0.5f,0.0f,0.0f,-1.0f,0.8125f,0.375f,0.5f,0.5f,0.5f,1.0f,0.0f,0.0f,0.6250f,0.375f,0.5f,-0.5f,-0.5f,1.0f,0.0f,0.0f,0.6875f,0.000f,0.5f,0.5f,-0.5f,1.0f,0.0f,0.0f,0.6875f,0.375f,0.5f,-0.5f,-0.5f,1.0f,0.0f,0.0f,0.6875f,0.000f,0.5f,0.5f,0.5f,1.0f,0.0f,0.0f,0.6250f,0.375f,0.5f,-0.5f,0.5f,1.0f,0.0f,0.0f,0.6250f,0.000f,-0.5f,0.5f,0.5f,-1.0f,0.0f,0.0f,0.7500f,0.375f,-0.5f,0.5f,-0.5f,-1.0f,0.0f,0.0f,0.8125f,0.375f,-0.5f,-0.5f,-0.5f,-1.0f,0.0f,0.0f,0.8125f,0.000f,-0.5f,-0.5f,-0.5f,-1.0f,0.0f,0.0f,0.8125f,0.000f,-0.5f,-0.5f,0.5f,-1.0f,0.0f,0.0f,0.7500f,0.000f,-0.5f,0.5f,0.5f,-1.0f,0.0f,0.0f,0.7500f,0.375f,-0.5f,0.5f,-0.5f,0.0f,1.0f,0.0f,0.6875f,0.375f,0.5f,0.5f,0.5f,0.0f,1.0f,0.0f,0.7500f,0.500f,0.5f,0.5f,-0.5f,0.0f,1.0f,0.0f,0.7500f,0.375f,0.5f,0.5f,0.5f,0.0f,1.0f,0.0f,0.7500f,0.500f,-0.5f,0.5f,-0.5f,0.0f,1.0f,0.0f,0.6875f,0.375f,-0.5f,0.5f,0.5f,0.0f,1.0f,0.0f,0.6875f,0.500f,-0.5f,-0.5f,-0.5f,0.0f,-1.0f,0.0f,0.7500f,0.375f,0.5f,-0.5f,-0.5f,0.0f,-1.0f,0.0f,0.8125f,0.375f,0.5f,-0.5f,0.5f,0.0f,-1.0f,0.0f,0.8125f,0.500f,0.5f,-0.5f,0.5f,0.0f,-1.0f,0.0f,0.8125f,0.500f,-0.5f,-0.5f,0.5f,0.0f,-1.0f,0.0f,0.7500f,0.500f,-0.5f,-0.5f,-0.5f,0.0f,-1.0f,0.0f,0.7500f,0.375f };
float legVertices[] = { -0.5f,-0.5f,0.5f,0.0f,0.0f,1.0f,0.0625f,0.000f,0.5f,-0.5f,0.5f,0.0f,0.0f,1.0f,0.1250f,0.000f,0.5f,0.5f,0.5f,0.0f,0.0f,1.0f,0.1250f,0.375f,0.5f,0.5f,0.5f,0.0f,0.0f,1.0f,0.1250f,0.375f,-0.5f,0.5f,0.5f,0.0f,0.0f,1.0f,0.0625f,0.375f,-0.5f,-0.5f,0.5f,0.0f,0.0f,1.0f,0.0625f,0.000f,-0.5f,-0.5f,-0.5f,0.0f,0.0f,-1.0f,0.1875f,0.000f,0.5f,0.5f,-0.5f,0.0f,0.0f,-1.0f,0.2500f,0.375f,0.5f,-0.5f,-0.5f,0.0f,0.0f,-1.0f,0.2500f,0.000f,0.5f,0.5f,-0.5f,0.0f,0.0f,-1.0f,0.2500f,0.375f,-0.5f,-0.5f,-0.5f,0.0f,0.0f,-1.0f,0.1875f,0.000f,-0.5f,0.5f,-0.5f,0.0f,0.0f,-1.0f,0.1875f,0.375f,0.5f,0.5f,0.5f,1.0f,0.0f,0.0f,0.0000f,0.375f,0.5f,-0.5f,-0.5f,1.0f,0.0f,0.0f,0.0625f,0.000f,0.5f,0.5f,-0.5f,1.0f,0.0f,0.0f,0.0625f,0.375f,0.5f,-0.5f,-0.5f,1.0f,0.0f,0.0f,0.0625f,0.000f,0.5f,0.5f,0.5f,1.0f,0.0f,0.0f,0.0000f,0.375f,0.5f,-0.5f,0.5f,1.0f,0.0f,0.0f,0.0000f,0.000f,-0.5f,0.5f,0.5f,-1.0f,0.0f,0.0f,0.1250f,0.375f,-0.5f,0.5f,-0.5f,-1.0f,0.0f,0.0f,0.1875f,0.375f,-0.5f,-0.5f,-0.5f,-1.0f,0.0f,0.0f,0.1875f,0.000f,-0.5f,-0.5f,-0.5f,-1.0f,0.0f,0.0f,0.1875f,0.000f,-0.5f,-0.5f,0.5f,-1.0f,0.0f,0.0f,0.1250f,0.000f,-0.5f,0.5f,0.5f,-1.0f,0.0f,0.0f,0.1250f,0.375f,-0.5f,0.5f,-0.5f,0.0f,1.0f,0.0f,0.0625f,0.375f,0.5f,0.5f,0.5f,0.0f,1.0f,0.0f,0.1250f,0.500f,0.5f,0.5f,-0.5f,0.0f,1.0f,0.0f,0.1250f,0.375f,0.5f,0.5f,0.5f,0.0f,1.0f,0.0f,0.1250f,0.500f,-0.5f,0.5f,-0.5f,0.0f,1.0f,0.0f,0.0625f,0.375f,-0.5f,0.5f,0.5f,0.0f,1.0f,0.0f,0.0625f,0.500f,-0.5f,-0.5f,-0.5f,0.0f,-1.0f,0.0f,0.1250f,0.375f,0.5f,-0.5f,-0.5f,0.0f,-1.0f,0.0f,0.1875f,0.375f,0.5f,-0.5f,0.5f,0.0f,-1.0f,0.0f,0.1875f,0.500f,0.5f,-0.5f,0.5f,0.0f,-1.0f,0.0f,0.1875f,0.500f,-0.5f,-0.5f,0.5f,0.0f,-1.0f,0.0f,0.1250f,0.500f,-0.5f,-0.5f,-0.5f,0.0f,-1.0f,0.0f,0.1250f,0.375f };
float grassSideVertices[] = { -0.5f,-0.5f,0.5f,0.0f,0.0f,1.0f,0.0f,0.0f,0.5f,-0.5f,0.5f,0.0f,0.0f,1.0f,1.0f,0.0f,0.5f,0.5f,0.5f,0.0f,0.0f,1.0f,1.0f,1.0f,0.5f,0.5f,0.5f,0.0f,0.0f,1.0f,1.0f,1.0f,-0.5f,0.5f,0.5f,0.0f,0.0f,1.0f,0.0f,1.0f,-0.5f,-0.5f,0.5f,0.0f,0.0f,1.0f,0.0f,0.0f,-0.5f,-0.5f,-0.5f,0.0f,0.0f,-1.0f,0.0f,0.0f,0.5f,0.5f,-0.5f,0.0f,0.0f,-1.0f,1.0f,1.0f,0.5f,-0.5f,-0.5f,0.0f,0.0f,-1.0f,1.0f,0.0f,0.5f,0.5f,-0.5f,0.0f,0.0f,-1.0f,1.0f,1.0f,-0.5f,-0.5f,-0.5f,0.0f,0.0f,-1.0f,0.0f,0.0f,-0.5f,0.5f,-0.5f,0.0f,0.0f,-1.0f,0.0f,1.0f,-0.5f,0.5f,0.5f,-1.0f,0.0f,0.0f,1.0f,1.0f,-0.5f,0.5f,-0.5f,-1.0f,0.0f,0.0f,0.0f,1.0f,-0.5f,-0.5f,-0.5f,-1.0f,0.0f,0.0f,0.0f,0.0f,-0.5f,-0.5f,-0.5f,-1.0f,0.0f,0.0f,0.0f,0.0f,-0.5f,-0.5f,0.5f,-1.0f,0.0f,0.0f,1.0f,0.0f,-0.5f,0.5f,0.5f,-1.0f,0.0f,0.0f,1.0f,1.0f,0.5f,0.5f,0.5f,1.0f,0.0f,0.0f,0.0f,1.0f,0.5f,-0.5f,-0.5f,1.0f,0.0f,0.0f,1.0f,0.0f,0.5f,0.5f,-0.5f,1.0f,0.0f,0.0f,1.0f,1.0f,0.5f,-0.5f,-0.5f,1.0f,0.0f,0.0f,1.0f,0.0f,0.5f,0.5f,0.5f,1.0f,0.0f,0.0f,0.0f,1.0f,0.5f,-0.5f,0.5f,1.0f,0.0f,0.0f,0.0f,0.0f };
float grassTopVertices[] = { -0.5f,0.5f,-0.5f,0.0f,1.0f,0.0f,0.0f,1.0f,0.5f,0.5f,0.5f,0.0f,1.0f,0.0f,1.0f,0.0f,0.5f,0.5f,-0.5f,0.0f,1.0f,0.0f,1.0f,1.0f,0.5f,0.5f,0.5f,0.0f,1.0f,0.0f,1.0f,0.0f,-0.5f,0.5f,-0.5f,0.0f,1.0f,0.0f,0.0f,1.0f,-0.5f,0.5f,0.5f,0.0f,1.0f,0.0f,0.0f,0.0f,-0.5f,-0.5f,-0.5f,0.0f,-1.0f,0.0f,0.0f,1.0f,0.5f,-0.5f,-0.5f,0.0f,-1.0f,0.0f,1.0f,1.0f,0.5f,-0.5f,0.5f,0.0f,-1.0f,0.0f,1.0f,0.0f,0.5f,-0.5f,0.5f,0.0f,-1.0f,0.0f,1.0f,0.0f,-0.5f,-0.5f,0.5f,0.0f,-1.0f,0.0f,0.0f,0.0f,-0.5f,-0.5f,-0.5f,0.0f,-1.0f,0.0f,0.0f,1.0f };
float standardBlockVertices[] = { -0.5f,-0.5f,0.5f,0.0f,0.0f,1.0f,0.0f,0.0f, 0.5f,-0.5f,0.5f,0.0f,0.0f,1.0f,1.0f,0.0f, 0.5f,0.5f,0.5f,0.0f,0.0f,1.0f,1.0f,1.0f, 0.5f,0.5f,0.5f,0.0f,0.0f,1.0f,1.0f,1.0f, -0.5f,0.5f,0.5f,0.0f,0.0f,1.0f,0.0f,1.0f, -0.5f,-0.5f,0.5f,0.0f,0.0f,1.0f,0.0f,0.0f, -0.5f,-0.5f,-0.5f,0.0f,0.0f,-1.0f,0.0f,0.0f, 0.5f,0.5f,-0.5f,0.0f,0.0f,-1.0f,1.0f,1.0f, 0.5f,-0.5f,-0.5f,0.0f,0.0f,-1.0f,1.0f,0.0f, 0.5f,0.5f,-0.5f,0.0f,0.0f,-1.0f,1.0f,1.0f, -0.5f,-0.5f,-0.5f,0.0f,0.0f,-1.0f,0.0f,0.0f, -0.5f,0.5f,-0.5f,0.0f,0.0f,-1.0f,0.0f,1.0f, -0.5f,0.5f,0.5f,-1.0f,0.0f,0.0f,1.0f,1.0f, -0.5f,0.5f,-0.5f,-1.0f,0.0f,0.0f,0.0f,1.0f, -0.5f,-0.5f,-0.5f,-1.0f,0.0f,0.0f,0.0f,0.0f, -0.5f,-0.5f,-0.5f,-1.0f,0.0f,0.0f,0.0f,0.0f, -0.5f,-0.5f,0.5f,-1.0f,0.0f,0.0f,1.0f,0.0f, -0.5f,0.5f,0.5f,-1.0f,0.0f,0.0f,1.0f,1.0f, 0.5f,0.5f,0.5f,1.0f,0.0f,0.0f,0.0f,1.0f, 0.5f,-0.5f,-0.5f,1.0f,0.0f,0.0f,1.0f,0.0f, 0.5f,0.5f,-0.5f,1.0f,0.0f,0.0f,1.0f,1.0f, 0.5f,-0.5f,-0.5f,1.0f,0.0f,0.0f,1.0f,0.0f, 0.5f,0.5f,0.5f,1.0f,0.0f,0.0f,0.0f,1.0f, 0.5f,-0.5f,0.5f,1.0f,0.0f,0.0f,0.0f,0.0f, -0.5f,0.5f,-0.5f,0.0f,1.0f,0.0f,0.0f,1.0f, 0.5f,0.5f,0.5f,0.0f,1.0f,0.0f,1.0f,0.0f, 0.5f,0.5f,-0.5f,0.0f,1.0f,0.0f,1.0f,1.0f, 0.5f,0.5f,0.5f,0.0f,1.0f,0.0f,1.0f,0.0f, -0.5f,0.5f,-0.5f,0.0f,1.0f,0.0f,0.0f,1.0f, -0.5f,0.5f,0.5f,0.0f,1.0f,0.0f,0.0f,0.0f, -0.5f,-0.5f,-0.5f,0.0f,-1.0f,0.0f,0.0f,1.0f, 0.5f,-0.5f,-0.5f,0.0f,-1.0f,0.0f,1.0f,1.0f, 0.5f,-0.5f,0.5f,0.0f,-1.0f,0.0f,1.0f,0.0f, 0.5f,-0.5f,0.5f,0.0f,-1.0f,0.0f,1.0f,0.0f, -0.5f,-0.5f,0.5f,0.0f,-1.0f,0.0f,0.0f,0.0f, -0.5f,-0.5f,-0.5f,0.0f,-1.0f,0.0f,0.0f,1.0f };

// 剑的顶点
float swordVertices[] = {
    // Pos           // Normal       // TexCoord
    -0.5f, -0.5f, 0.0f,  0.0f, 0.0f, 1.0f,  0.0f, 0.0f,
     0.5f, -0.5f, 0.0f,  0.0f, 0.0f, 1.0f,  1.0f, 0.0f,
     0.5f,  0.5f, 0.0f,  0.0f, 0.0f, 1.0f,  1.0f, 1.0f,
     0.5f,  0.5f, 0.0f,  0.0f, 0.0f, 1.0f,  1.0f, 1.0f,
    -0.5f,  0.5f, 0.0f,  0.0f, 0.0f, 1.0f,  0.0f, 1.0f,
    -0.5f, -0.5f, 0.0f,  0.0f, 0.0f, 1.0f,  0.0f, 0.0f
};

// ==========================================
// 函数声明
// ==========================================
unsigned int loadTexture(const char* path);
unsigned int createShader(const char* vSrc, const char* fSrc, bool isSource);
unsigned int createShaderFromFile(const char* vPath, const char* fPath);
void processInput(GLFWwindow* window);
void mouse_callback(GLFWwindow* window, double xpos, double ypos);
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset);
void framebuffer_size_callback(GLFWwindow* window, int width, int height);
void createTree(float x, float z, float groundY);
void renderScene(unsigned int shaderProgram, bool isShadowPass);
void generateSkyblockIsland();
void updateBlockHighlight();

// 树木生成
void createTree(float x, float z, float groundY) {
    int height = 4 + (rand() % 3);
    for (int i = 1; i <= height; i++) {
        glm::vec3 pos(x, groundY + i, z);
        trunkBlocks.push_back(pos);
        collisionBlocks.push_back(pos);
    }
    int leavesStart = height - 2;
    int leavesEnd = height + 1;
    for (int y = leavesStart; y <= leavesEnd; y++) {
        int radius = (y >= leavesEnd - 1) ? 1 : 2;
        for (int lx = -radius; lx <= radius; lx++) {
            for (int lz = -radius; lz <= radius; lz++) {
                if (radius == 2 && abs(lx) == 2 && abs(lz) == 2) { if (rand() % 2 == 0) continue; }
                if (y <= height && lx == 0 && lz == 0) continue;
                glm::vec3 pos(x + lx, groundY + y, z + lz);
                leafBlocks.push_back(pos);
                collisionBlocks.push_back(pos);
            }
        }
    }
}

// 空岛生成
void generateSkyblockIsland() {
    int radius = 6;
    for (int x = -radius; x <= radius; x++) {
        for (int z = -radius; z <= radius; z++) {
            float dist = sqrt(x * x + z * z);
            if (dist > radius) continue;
            int maxDepth = (int)(7.0f * (1.0f - dist / (float)radius));
            if (maxDepth < 1) maxDepth = 1;
            for (int y = 0; y >= -maxDepth; y--) {
                glm::vec3 pos(x * 1.0f, y * 1.0f, z * 1.0f);
                BlockType type;
                if (y == 0) type = GRASS_BLOCK;
                else if (y >= -2) type = DIRT;
                else {
                    int r = rand() % 100;
                    if (r < 10) type = COAL_ORE; else if (r < 15) type = IRON_ORE; else type = STONE;
                }
                terrainBlocks.push_back({ pos, type });
                collisionBlocks.push_back(pos);
            }
        }
    }
    createTree(-2.0f, -2.0f, 0.0f);

    // 箱子初始化
    chestPos = glm::vec3(2.0f, 1.0f, 2.0f);
}

// 场景绘制
void renderScene(unsigned int shaderProgram, bool isShadowPass) {
    glBindVertexArray(blockVAO);

    // 1. 地形
    for (const auto& block : terrainBlocks) {
        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, block.position);
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "model"), 1, GL_FALSE, &model[0][0]);

        if (!isShadowPass) {
            if (block.type == GRASS_BLOCK) {
                glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, grassSideTex);
                glDrawArrays(GL_TRIANGLES, 0, 24);
                glBindTexture(GL_TEXTURE_2D, grassTopTex);
                glDrawArrays(GL_TRIANGLES, 24, 6);
                glBindTexture(GL_TEXTURE_2D, dirtTex);
                glDrawArrays(GL_TRIANGLES, 30, 6);
            }
            else {
                if (block.type == DIRT) glBindTexture(GL_TEXTURE_2D, dirtTex);
                else if (block.type == STONE) glBindTexture(GL_TEXTURE_2D, stoneTex);
                else if (block.type == COAL_ORE) glBindTexture(GL_TEXTURE_2D, coalTex);
                else if (block.type == IRON_ORE) glBindTexture(GL_TEXTURE_2D, ironTex);
                glActiveTexture(GL_TEXTURE0);
                glDrawArrays(GL_TRIANGLES, 0, 36);
            }
        }
        else {
            glDrawArrays(GL_TRIANGLES, 0, 36);
        }
    }

    // 2. 绘制箱子 (支持移动/旋转)
    glm::mat4 chestModel = glm::mat4(1.0f);
    chestModel = glm::translate(chestModel, chestPos);
    chestModel = glm::rotate(chestModel, glm::radians(chestRotation), glm::vec3(0.0f, 1.0f, 0.0f));

    glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "model"), 1, GL_FALSE, &chestModel[0][0]);
    if (!isShadowPass) {
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, chestFrontTex);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindTexture(GL_TEXTURE_2D, chestSideTex);
        glDrawArrays(GL_TRIANGLES, 6, 18);
        glBindTexture(GL_TEXTURE_2D, chestTopTex);
        glDrawArrays(GL_TRIANGLES, 24, 12);
    }
    else {
        glDrawArrays(GL_TRIANGLES, 0, 36);
    }

    // 3. 树木
    glBindVertexArray(blockVAO);
    if (!isShadowPass) { glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, logTex); }
    for (const auto& pos : trunkBlocks) {
        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, pos);
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "model"), 1, GL_FALSE, &model[0][0]);
        if (!isShadowPass) glDrawArrays(GL_TRIANGLES, 0, 24);
        else glDrawArrays(GL_TRIANGLES, 0, 36);
    }
    if (!isShadowPass) {
        glBindTexture(GL_TEXTURE_2D, logTopTex);
        for (const auto& pos : trunkBlocks) {
            glm::mat4 model = glm::mat4(1.0f);
            model = glm::translate(model, pos);
            glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "model"), 1, GL_FALSE, &model[0][0]);
            glDrawArrays(GL_TRIANGLES, 24, 12);
        }
    }

    // 树叶 (阴影Pass处理Alpha)
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, leafTex);
    if (isShadowPass) {
        glUniform1i(glGetUniformLocation(shaderProgram, "hasTexture"), 1);
    }
    for (const auto& pos : leafBlocks) {
        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, pos);
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "model"), 1, GL_FALSE, &model[0][0]);
        glDrawArrays(GL_TRIANGLES, 0, 36);
    }
    if (isShadowPass) {
        glUniform1i(glGetUniformLocation(shaderProgram, "hasTexture"), 0);
    }

    // 4. 渲染机器人
    if (isShadowPass || currentCamMode != FIRST_PERSON) {
        if (!isShadowPass) { glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, charTexture); }

        // 应用视觉偏移
        glm::vec3 renderPos = robotPos + visualOffset;

        glBindVertexArray(torsoVAO);
        glm::mat4 modelTorso = glm::mat4(1.0f);
        modelTorso = glm::translate(modelTorso, renderPos);
        modelTorso = glm::rotate(modelTorso, glm::radians(robotRot), glm::vec3(0.0f, 1.0f, 0.0f));
        glm::mat4 parentMatrix = modelTorso;
        modelTorso = glm::scale(modelTorso, glm::vec3(0.5f, 0.75f, 0.25f));
        modelTorso = glm::translate(modelTorso, glm::vec3(0.0f, 1.0f, 0.0f));
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "model"), 1, GL_FALSE, &modelTorso[0][0]);
        glDrawArrays(GL_TRIANGLES, 0, 36);

        glBindVertexArray(headVAO);
        glm::mat4 modelHead = parentMatrix;
        modelHead = glm::translate(modelHead, glm::vec3(0.0f, 1.375f, 0.0f));
        modelHead = glm::scale(modelHead, glm::vec3(0.5f, 0.5f, 0.5f));
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "model"), 1, GL_FALSE, &modelHead[0][0]);
        glDrawArrays(GL_TRIANGLES, 0, 36);

        glBindVertexArray(limbVAO);
        glm::mat4 modelLArm = parentMatrix; modelLArm = glm::translate(modelLArm, glm::vec3(-0.38f, 1.1f, 0.0f)); modelLArm = glm::rotate(modelLArm, glm::radians(armAngle), glm::vec3(1.0f, 0.0f, 0.0f)); modelLArm = glm::scale(modelLArm, glm::vec3(0.25f, 0.75f, 0.25f)); modelLArm = glm::translate(modelLArm, glm::vec3(0.0f, -0.5f, 0.0f));
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "model"), 1, GL_FALSE, &modelLArm[0][0]); glDrawArrays(GL_TRIANGLES, 0, 36);

        glm::mat4 rArmPivot = parentMatrix;
        rArmPivot = glm::translate(rArmPivot, glm::vec3(0.38f, 1.1f, 0.0f));
        rArmPivot = glm::rotate(rArmPivot, glm::radians(-armAngle), glm::vec3(1.0f, 0.0f, 0.0f));

        glm::mat4 modelRArm = rArmPivot;
        modelRArm = glm::scale(modelRArm, glm::vec3(0.25f, 0.75f, 0.25f));
        modelRArm = glm::translate(modelRArm, glm::vec3(0.0f, -0.5f, 0.0f));
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "model"), 1, GL_FALSE, &modelRArm[0][0]); glDrawArrays(GL_TRIANGLES, 0, 36);

        // 渲染腿部
        glBindVertexArray(legVAO);
        glm::mat4 modelLLeg = parentMatrix; modelLLeg = glm::translate(modelLLeg, glm::vec3(-0.13f, 0.375f, 0.0f)); modelLLeg = glm::rotate(modelLLeg, glm::radians(-armAngle), glm::vec3(1.0f, 0.0f, 0.0f)); modelLLeg = glm::scale(modelLLeg, glm::vec3(0.25f, 0.75f, 0.25f)); modelLLeg = glm::translate(modelLLeg, glm::vec3(0.0f, -0.5f, 0.0f));
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "model"), 1, GL_FALSE, &modelLLeg[0][0]); glDrawArrays(GL_TRIANGLES, 0, 36);
        glm::mat4 modelRLeg = parentMatrix; modelRLeg = glm::translate(modelRLeg, glm::vec3(0.13f, 0.375f, 0.0f)); modelRLeg = glm::rotate(modelRLeg, glm::radians(armAngle), glm::vec3(1.0f, 0.0f, 0.0f)); modelRLeg = glm::scale(modelRLeg, glm::vec3(0.25f, 0.75f, 0.25f)); modelRLeg = glm::translate(modelRLeg, glm::vec3(0.0f, -0.5f, 0.0f));
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "model"), 1, GL_FALSE, &modelRLeg[0][0]); glDrawArrays(GL_TRIANGLES, 0, 36);

        // =====================================
        // 渲染钻石剑
        // =====================================
        glm::mat4 modelSword = rArmPivot;

        // 位置修正
        modelSword = glm::translate(modelSword, glm::vec3(-0.06f, -0.65f, -0.2f));

        // 旋转调整
        modelSword = glm::rotate(modelSword, glm::radians(270.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        modelSword = glm::rotate(modelSword, glm::radians(-45.0f), glm::vec3(0.0f, 0.0f, 1.0f));

        // 轴心修正
        modelSword = glm::translate(modelSword, glm::vec3(0.5f, 0.5f, 0.0f));
        modelSword = glm::scale(modelSword, glm::vec3(0.8f));

        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "model"), 1, GL_FALSE, &modelSword[0][0]);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, swordTexture);

        if (isShadowPass) {
            glUniform1i(glGetUniformLocation(shaderProgram, "hasTexture"), 1);
        }

        glDisable(GL_CULL_FACE);
        glBindVertexArray(swordVAO);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glEnable(GL_CULL_FACE);

        if (isShadowPass) {
            glUniform1i(glGetUniformLocation(shaderProgram, "hasTexture"), 0);
        }
        // =====================================
    }
}

// ==========================================
// Main
// ==========================================
int main()
{
    srand(static_cast<unsigned int>(time(0)));
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
    GLFWwindow* window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "2023150012-Yangdi-finalproject", NULL, NULL);
    if (window == NULL) { glfwTerminate(); return -1; }
    glfwMakeContextCurrent(window);
    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetScrollCallback(window, scroll_callback);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) return -1;
    glEnable(GL_DEPTH_TEST);
    // 开启面剔除
    glEnable(GL_CULL_FACE);

    // 开启混合
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // 1. 初始化着色器
    unsigned int mainShader = createShaderFromFile("shaders/vshader.glsl", "shaders/fshader.glsl");
    unsigned int depthShader = createShader(simpleDepthVertexShaderSource, simpleDepthFragmentShaderSource, true);

    // 2. 初始化阴影帧缓冲 (FBO)
    glGenFramebuffers(1, &depthMapFBO);
    glGenTextures(1, &depthMapTex);
    glBindTexture(GL_TEXTURE_2D, depthMapTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, SHADOW_WIDTH, SHADOW_HEIGHT, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    float borderColor[] = { 1.0, 1.0, 1.0, 1.0 };
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);
    glBindFramebuffer(GL_FRAMEBUFFER, depthMapFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthMapTex, 0);
    glDrawBuffer(GL_NONE); glReadBuffer(GL_NONE);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // VBO定义
    unsigned int headVBO, torsoVBO, limbVBO, legVBO, blockVBO;

    // === VAO 配置 ===
    glGenVertexArrays(1, &headVAO); glGenBuffers(1, &headVBO); glBindVertexArray(headVAO); glBindBuffer(GL_ARRAY_BUFFER, headVBO); glBufferData(GL_ARRAY_BUFFER, sizeof(headVertices), headVertices, GL_STATIC_DRAW); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0); glEnableVertexAttribArray(0); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float))); glEnableVertexAttribArray(1); glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float))); glEnableVertexAttribArray(2);
    glGenVertexArrays(1, &torsoVAO); glGenBuffers(1, &torsoVBO); glBindVertexArray(torsoVAO); glBindBuffer(GL_ARRAY_BUFFER, torsoVBO); glBufferData(GL_ARRAY_BUFFER, sizeof(torsoVertices), torsoVertices, GL_STATIC_DRAW); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0); glEnableVertexAttribArray(0); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float))); glEnableVertexAttribArray(1); glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float))); glEnableVertexAttribArray(2);
    glGenVertexArrays(1, &limbVAO); glGenBuffers(1, &limbVBO); glBindVertexArray(limbVAO); glBindBuffer(GL_ARRAY_BUFFER, limbVBO); glBufferData(GL_ARRAY_BUFFER, sizeof(limbVertices), limbVertices, GL_STATIC_DRAW); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0); glEnableVertexAttribArray(0); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float))); glEnableVertexAttribArray(1); glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float))); glEnableVertexAttribArray(2);
    glGenVertexArrays(1, &legVAO); glGenBuffers(1, &legVBO); glBindVertexArray(legVAO); glBindBuffer(GL_ARRAY_BUFFER, legVBO); glBufferData(GL_ARRAY_BUFFER, sizeof(legVertices), legVertices, GL_STATIC_DRAW); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0); glEnableVertexAttribArray(0); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float))); glEnableVertexAttribArray(1); glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float))); glEnableVertexAttribArray(2);
    glGenVertexArrays(1, &blockVAO); glGenBuffers(1, &blockVBO); glBindVertexArray(blockVAO); glBindBuffer(GL_ARRAY_BUFFER, blockVBO); glBufferData(GL_ARRAY_BUFFER, sizeof(standardBlockVertices), standardBlockVertices, GL_STATIC_DRAW); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0); glEnableVertexAttribArray(0); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float))); glEnableVertexAttribArray(1); glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float))); glEnableVertexAttribArray(2);

    // 配置剑的 VAO
    glGenVertexArrays(1, &swordVAO);
    glGenBuffers(1, &swordVBO);
    glBindVertexArray(swordVAO);
    glBindBuffer(GL_ARRAY_BUFFER, swordVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(swordVertices), swordVertices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);


    // === Textures ===
    charTexture = loadTexture("assets/char.png");
    grassSideTex = loadTexture("assets/grass_side.png");
    grassTopTex = loadTexture("assets/grass_top.png");
    dirtTex = loadTexture("assets/dirt.png");
    stoneTex = loadTexture("assets/stone.png");
    coalTex = loadTexture("assets/coal_ore.png");
    ironTex = loadTexture("assets/iron_ore.png");
    logTex = loadTexture("assets/log.png");
    logTopTex = loadTexture("assets/log_top.png");
    leafTex = loadTexture("assets/leaf.png");
    chestFrontTex = loadTexture("assets/chest_front.png");
    chestSideTex = loadTexture("assets/chest_side.png");
    chestTopTex = loadTexture("assets/chest_top.png");

    // 加载剑的纹理
    swordTexture = loadTexture("assets/Diamond_Sword.png");

    // Shader 配置
    glUseProgram(mainShader);
    glUniform1i(glGetUniformLocation(mainShader, "texture1"), 0);
    glUniform1i(glGetUniformLocation(mainShader, "shadowMap"), 1);

    generateSkyblockIsland();

    // ===========================
    // 循环
    // ===========================
    while (!glfwWindowShouldClose(window))
    {
        float currentFrame = static_cast<float>(glfwGetTime());
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        // 1. 昼夜 & 光照位置
        float timeValue = (float)glfwGetTime();
        float daySpeed = 0.1f;
        float sunX = sin(timeValue * daySpeed) * 30.0f;
        float sunY = cos(timeValue * daySpeed) * 30.0f;
        lightPos = glm::vec3(sunX, sunY, sunX * 0.2f);

        // 2. 物理
        glm::vec3 proposedMove(0.0f);
        float robotSpeed = 4.0f * deltaTime;
        if (currentCamMode != FREE_CAM) {
            if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) proposedMove += glm::normalize(glm::vec3(cos(glm::radians(yaw)), 0, sin(glm::radians(yaw)))) * robotSpeed;
            if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) proposedMove -= glm::normalize(glm::vec3(cos(glm::radians(yaw)), 0, sin(glm::radians(yaw)))) * robotSpeed;
            if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) proposedMove -= glm::normalize(glm::cross(glm::vec3(cos(glm::radians(yaw)), 0, sin(glm::radians(yaw))), cameraUp)) * robotSpeed;
            if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) proposedMove += glm::normalize(glm::cross(glm::vec3(cos(glm::radians(yaw)), 0, sin(glm::radians(yaw))), cameraUp)) * robotSpeed;
            robotRot = -yaw - 90.0f;
        }
        else {
            // Free cam
            float camSpeed = 10.0f * deltaTime;
            if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) cameraPos += camSpeed * cameraFront;
            if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) cameraPos -= camSpeed * cameraFront;
            if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) cameraPos -= glm::normalize(glm::cross(cameraFront, cameraUp)) * camSpeed;
            if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) cameraPos += glm::normalize(glm::cross(cameraFront, cameraUp)) * camSpeed;
        }

        // Collision logic
        if (abs(proposedMove.x) > 0.0001f) {
            AABB boxX = getRobotAABB(robotPos + glm::vec3(proposedMove.x, 0.0f, 0.0f));
            if (!checkSceneCollision(boxX)) robotPos.x += proposedMove.x;
        }
        if (abs(proposedMove.z) > 0.0001f) {
            AABB boxZ = getRobotAABB(robotPos + glm::vec3(0.0f, 0.0f, proposedMove.z));
            if (!checkSceneCollision(boxZ)) robotPos.z += proposedMove.z;
        }
        if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS && isGrounded && currentCamMode != FREE_CAM) { verticalVelocity = jumpStrength; isGrounded = false; }
        verticalVelocity -= gravity * deltaTime;
        float dy = verticalVelocity * deltaTime;
        AABB boxY = getRobotAABB(robotPos + glm::vec3(0.0f, dy, 0.0f));
        if (checkSceneCollision(boxY)) { if (verticalVelocity < 0) isGrounded = true; verticalVelocity = 0.0f; }
        else { robotPos.y += dy; isGrounded = false; }
        if (robotPos.y < -30.0f) { robotPos = glm::vec3(0.0f, 6.0f, 0.0f); verticalVelocity = 0.0f; }
        if (glm::length(proposedMove) > 0.001f) { if (armDir) armAngle += 150.0f * deltaTime; else armAngle -= 150.0f * deltaTime; if (armAngle > 45.0f || armAngle < -45.0f) armDir = !armDir; }
        else armAngle = glm::mix(armAngle, 0.0f, 10.0f * deltaTime);

        processInput(window);

        // =====================================
        // 渲染流程 (Two Pass)
        // =====================================

        // 1. Shadow Pass
        glm::mat4 lightProjection, lightView;
        float near_plane = 1.0f, far_plane = 50.0f;
        lightProjection = glm::ortho(-20.0f, 20.0f, -20.0f, 20.0f, near_plane, far_plane);
        lightView = glm::lookAt(lightPos, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        glm::mat4 lightSpaceMatrix = lightProjection * lightView;

        glUseProgram(depthShader);
        glUniformMatrix4fv(glGetUniformLocation(depthShader, "lightSpaceMatrix"), 1, GL_FALSE, &lightSpaceMatrix[0][0]);

        glViewport(0, 0, SHADOW_WIDTH, SHADOW_HEIGHT);
        glBindFramebuffer(GL_FRAMEBUFFER, depthMapFBO);
        glClear(GL_DEPTH_BUFFER_BIT);
        glCullFace(GL_FRONT);
        renderScene(depthShader, true);
        glCullFace(GL_BACK);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        // 2. Lighting Pass
        glViewport(0, 0, SCR_WIDTH, SCR_HEIGHT);
        glm::vec3 dayColor(0.5f, 0.7f, 0.9f);
        glm::vec3 nightColor(0.05f, 0.05f, 0.1f);
        float mixFactor = (cos(timeValue * daySpeed) + 1.0f) / 2.0f;
        glm::vec3 skyColor = glm::mix(nightColor, dayColor, glm::clamp(mixFactor * 1.5f - 0.25f, 0.0f, 1.0f));

        glClearColor(skyColor.r, skyColor.g, skyColor.b, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glUseProgram(mainShader);
        glUniform3fv(glGetUniformLocation(mainShader, "lightPos"), 1, &lightPos[0]);
        glUniform3fv(glGetUniformLocation(mainShader, "viewPos"), 1, &cameraPos[0]);
        glUniform3fv(glGetUniformLocation(mainShader, "skyColor"), 1, &skyColor[0]);
        glUniformMatrix4fv(glGetUniformLocation(mainShader, "lightSpaceMatrix"), 1, GL_FALSE, &lightSpaceMatrix[0][0]);

        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, depthMapTex);

        // 相机矩阵
        glm::mat4 view;
        if (currentCamMode == THIRD_PERSON) {
            cameraPos = robotPos + glm::vec3(0.0f, 1.6f, 0.0f) - glm::normalize(cameraFront) * 4.0f;
            view = glm::lookAt(cameraPos, robotPos + glm::vec3(0.0f, 1.6f, 0.0f), cameraUp);
        }
        else if (currentCamMode == FIRST_PERSON) {
            cameraPos = robotPos + glm::vec3(0.0f, 1.6f, 0.0f);
            view = glm::lookAt(cameraPos, cameraPos + cameraFront, cameraUp);
        }
        else {
            view = glm::lookAt(cameraPos, cameraPos + cameraFront, cameraUp);
        }
        glm::mat4 projection = glm::perspective(glm::radians(fov), (float)SCR_WIDTH / (float)SCR_HEIGHT, 0.1f, 100.0f);
        glUniformMatrix4fv(glGetUniformLocation(mainShader, "projection"), 1, GL_FALSE, &projection[0][0]);
        glUniformMatrix4fv(glGetUniformLocation(mainShader, "view"), 1, GL_FALSE, &view[0][0]);

        renderScene(mainShader, false);

        if (hasSelection) {
            glm::mat4 model = glm::mat4(1.0f);
            model = glm::translate(model, selectedBlockPos);
            model = glm::scale(model, glm::vec3(1.01f));
            glUniformMatrix4fv(glGetUniformLocation(mainShader, "model"), 1, GL_FALSE, &model[0][0]);
            glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
            glDisable(GL_TEXTURE_2D);
            glBindVertexArray(blockVAO);
            glDrawArrays(GL_TRIANGLES, 0, 36);
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
            glEnable(GL_TEXTURE_2D);
        }

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glDeleteVertexArrays(1, &headVAO); glDeleteVertexArrays(1, &torsoVAO); glDeleteVertexArrays(1, &limbVAO);
    glDeleteVertexArrays(1, &legVAO); glDeleteVertexArrays(1, &blockVAO);
    glDeleteVertexArrays(1, &swordVAO);

    glfwTerminate();
    return 0;
}

// 辅助函数
void processInput(GLFWwindow* window) {
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) glfwSetWindowShouldClose(window, true);
    if (glfwGetKey(window, GLFW_KEY_V) == GLFW_PRESS) { if (!vKeyPressed) { currentCamMode = (currentCamMode + 1) % 3; vKeyPressed = true; } }
    else vKeyPressed = false;

    // ==============================================
    // 箱子控制逻辑
    // ==============================================
    float objSpeed = 2.0f * deltaTime;
    // I/K: 前后移动 (Z轴)
    if (glfwGetKey(window, GLFW_KEY_I) == GLFW_PRESS) chestPos.z -= objSpeed;
    if (glfwGetKey(window, GLFW_KEY_K) == GLFW_PRESS) chestPos.z += objSpeed;
    // J/L: 左右移动 (X轴)
    if (glfwGetKey(window, GLFW_KEY_J) == GLFW_PRESS) chestPos.x -= objSpeed;
    if (glfwGetKey(window, GLFW_KEY_L) == GLFW_PRESS) chestPos.x += objSpeed;
    // U/O: 旋转箱子
    if (glfwGetKey(window, GLFW_KEY_U) == GLFW_PRESS) chestRotation += 60.0f * deltaTime;
    if (glfwGetKey(window, GLFW_KEY_O) == GLFW_PRESS) chestRotation -= 60.0f * deltaTime;
}

void mouse_callback(GLFWwindow* window, double xposIn, double yposIn) {
    float xpos = static_cast<float>(xposIn); float ypos = static_cast<float>(yposIn);
    if (firstMouse) { lastX = xpos; lastY = ypos; firstMouse = false; }
    float xoffset = xpos - lastX; float yoffset = lastY - ypos;
    lastX = xpos; lastY = ypos;
    xoffset *= 0.1f; yoffset *= 0.1f;
    yaw += xoffset; pitch += yoffset;
    if (pitch > 89.0f) pitch = 89.0f; if (pitch < -89.0f) pitch = -89.0f;
    glm::vec3 front;
    front.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
    front.y = sin(glm::radians(pitch));
    front.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
    cameraFront = glm::normalize(front);
}
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset) { fov -= (float)yoffset; if (fov < 1.0f) fov = 1.0f; if (fov > 45.0f) fov = 45.0f; }

void framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
    glViewport(0, 0, width, height);
    SCR_WIDTH = width;
    SCR_HEIGHT = height;
}

unsigned int loadTexture(const char* path) {
    unsigned int textureID; glGenTextures(1, &textureID);
    int width, height, nrComponents;
    stbi_set_flip_vertically_on_load(true);
    unsigned char* data = stbi_load(path, &width, &height, &nrComponents, 4);
    if (data) {
        glBindTexture(GL_TEXTURE_2D, textureID);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        stbi_image_free(data);
    }
    else { std::cout << "Texture failed to load at path: " << path << std::endl; stbi_image_free(data); }
    return textureID;
}

unsigned int createShader(const char* vSrc, const char* fSrc, bool isSource) {
    const char* vCode = vSrc;
    const char* fCode = fSrc;
    unsigned int vertex, fragment; int success; char infoLog[512];
    vertex = glCreateShader(GL_VERTEX_SHADER); glShaderSource(vertex, 1, &vCode, NULL); glCompileShader(vertex);
    glGetShaderiv(vertex, GL_COMPILE_STATUS, &success); if (!success) { glGetShaderInfoLog(vertex, 512, NULL, infoLog); std::cout << "ERROR::VERTEX::COMPILATION_FAILED\n" << infoLog << std::endl; }
    fragment = glCreateShader(GL_FRAGMENT_SHADER); glShaderSource(fragment, 1, &fCode, NULL); glCompileShader(fragment);
    glGetShaderiv(fragment, GL_COMPILE_STATUS, &success); if (!success) { glGetShaderInfoLog(fragment, 512, NULL, infoLog); std::cout << "ERROR::FRAGMENT::COMPILATION_FAILED\n" << infoLog << std::endl; }
    unsigned int ID = glCreateProgram(); glAttachShader(ID, vertex); glAttachShader(ID, fragment); glLinkProgram(ID);
    glDeleteShader(vertex); glDeleteShader(fragment);
    return ID;
}

unsigned int createShaderFromFile(const char* vPath, const char* fPath) {
    std::string vertexCode; std::string fragmentCode;
    std::ifstream vShaderFile; std::ifstream fShaderFile;
    vShaderFile.exceptions(std::ifstream::failbit | std::ifstream::badbit);
    fShaderFile.exceptions(std::ifstream::failbit | std::ifstream::badbit);
    try {
        vShaderFile.open(vPath); fShaderFile.open(fPath);
        std::stringstream vShaderStream, fShaderStream;
        vShaderStream << vShaderFile.rdbuf(); fShaderStream << fShaderFile.rdbuf();
        vShaderFile.close(); fShaderFile.close();
        vertexCode = vShaderStream.str(); fragmentCode = fShaderStream.str();
    }
    catch (std::ifstream::failure& e) { std::cout << "ERROR::SHADER::FILE_NOT_SUCCESFULLY_READ" << std::endl; }
    return createShader(vertexCode.c_str(), fragmentCode.c_str(), true);
}