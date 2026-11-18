#pragma once

#include <QImage>
#include <QJsonObject>
#include <QString>
#include <QVector>

#include <array>
#include <cstdint>
#include <vector>

namespace lumatrace::scene {

struct Vec2 { float x = 0.f, y = 0.f; };
struct Vec3 { float x = 0.f, y = 0.f, z = 0.f; };
struct Vec4 { float x = 0.f, y = 0.f, z = 0.f, w = 1.f; };
using Matrix4 = std::array<float, 16>; // glTF column-major

struct Vertex {
    Vec3 position;
    Vec3 normal;
    Vec2 uv0;
    Vec4 tangent;
    Vec4 color{1.f, 1.f, 1.f, 1.f};
};

struct TextureReference {
    int texture = -1;
    int texCoord = 0;
    QJsonObject extensions;
};

struct Texture {
    QString name;
    QString sourcePath;
    QImage image;
    QJsonObject sampler;
};

struct Material {
    QString name;
    Vec4 baseColor{1.f, 1.f, 1.f, 1.f};
    float metallic = 1.f;
    float roughness = 1.f;
    Vec3 emissive{0.f, 0.f, 0.f};
    QString alphaMode = QStringLiteral("OPAQUE");
    float alphaCutoff = 0.5f;
    bool doubleSided = false;
    float normalScale = 1.f;
    float occlusionStrength = 1.f;
    TextureReference baseColorTexture;
    TextureReference metallicRoughnessTexture;
    TextureReference normalTexture;
    TextureReference occlusionTexture;
    TextureReference emissiveTexture;
    QJsonObject extensions; // retain KHR_materials_* data not yet used by a shader
};

struct Primitive {
    QString nodeName;
    QString meshName;
    Matrix4 worldTransform{};
    int material = -1;
    QJsonObject extensions;
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
};

struct Bounds { Vec3 minimum, maximum; bool valid = false; };

struct Scene {
    QString name;
    QString rootPath;
    QString modelPath;
    Bounds bounds;
    QVector<Texture> textures;
    QVector<Material> materials;
    QVector<Primitive> primitives;
    std::uint64_t triangleCount = 0;
    QJsonObject environment;
};

struct LoadResult {
    Scene scene;
    QString error;
    explicit operator bool() const { return error.isEmpty(); }
};

// Transactional: callers replace their active scene only after a complete,
// validated manifest, model, geometry, material, and texture load succeeds.
LoadResult loadScene(const QString& manifestPath);

} // namespace lumatrace::scene
