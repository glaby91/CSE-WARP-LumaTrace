#include "scene_loader.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QUrl>

#include <QtEndian>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <optional>
#include <utility>

namespace lumatrace::scene {
namespace {
constexpr quint32 kGlbJson = 0x4E4F534A;
constexpr quint32 kGlbBin = 0x004E4942;

struct GltfData {
    QJsonObject json;
    QVector<QByteArray> buffers;
};

bool fail(QString& error, const QString& message) {
    error = message;
    return false;
}

float finiteFloat(double value, float fallback = 0.f) {
    if (!std::isfinite(value) || value < -double(std::numeric_limits<float>::max()) ||
        value > double(std::numeric_limits<float>::max())) return fallback;
    return float(value);
}

bool jsonVec(const QJsonValue& value, int count, float* out, QString& error,
             const QString& field) {
    if (!value.isArray() || value.toArray().size() != count)
        return fail(error, QStringLiteral("Invalid %1 vector.").arg(field));
    const auto a = value.toArray();
    for (int i = 0; i < count; ++i) {
        if (!a[i].isDouble() || !std::isfinite(a[i].toDouble()))
            return fail(error, QStringLiteral("Non-finite %1 value.").arg(field));
        out[i] = float(a[i].toDouble());
    }
    return true;
}

QByteArray decodeDataUri(const QString& uri, QString& error, diagnostics::RunLogger* logger) {
    std::optional<diagnostics::RunLogger::Scope> timing;
    if(logger)timing.emplace(logger->scope(QStringLiteral("decodeDataUri · base64/url")));
    const int comma = uri.indexOf(QLatin1Char(','));
    if (!uri.startsWith(QStringLiteral("data:")) || comma < 0) {
        error = QStringLiteral("Malformed glTF data URI.");
        return {};
    }
    const QString metadata = uri.left(comma);
    const QByteArray payload = uri.mid(comma + 1).toLatin1();
    if (metadata.endsWith(QStringLiteral(";base64"))) {
        const QByteArray decoded = QByteArray::fromBase64(payload, QByteArray::AbortOnBase64DecodingErrors);
        if (decoded.isEmpty() && !payload.isEmpty()) error = QStringLiteral("Invalid base64 glTF data URI.");
        return decoded;
    }
    return QUrl::fromPercentEncoding(payload).toUtf8();
}

bool readJsonObject(const QString& path, QJsonObject& object, QString& error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return fail(error, QStringLiteral("Cannot open scene file %1: %2").arg(path, file.errorString()));
    QJsonParseError parse;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parse);
    if (parse.error != QJsonParseError::NoError || !doc.isObject())
        return fail(error, QStringLiteral("Invalid JSON in %1: %2").arg(path, parse.errorString()));
    object = doc.object();
    return true;
}

bool safeFile(const QString& root, const QString& relative, QString& absolute, QString& error) {
    if (relative.isEmpty() || QDir::isAbsolutePath(relative))
        return fail(error, QStringLiteral("Scene resource path must be relative: %1").arg(relative));
    const QString rootCanonical = QFileInfo(root).canonicalFilePath();
    const QString candidate = QDir::cleanPath(QDir(root).absoluteFilePath(relative));
    const QString canonical = QFileInfo(candidate).canonicalFilePath();
    if (rootCanonical.isEmpty() || canonical.isEmpty())
        return fail(error, QStringLiteral("Missing scene resource: %1").arg(relative));
    const QString relativePath = QDir(rootCanonical).relativeFilePath(canonical);
    if (relativePath == QStringLiteral("..") || relativePath.startsWith(QStringLiteral("../")) ||
        relativePath.startsWith(QStringLiteral("..\\")) || QDir::isAbsolutePath(relativePath))
        return fail(error, QStringLiteral("Scene resource escapes its root: %1").arg(relativePath));
    absolute = canonical;
    return true;
}

bool readUri(const QString& baseDir, const QString& uri, QByteArray& bytes,
             QString* canonicalPath, QString& error, diagnostics::RunLogger* logger) {
    std::optional<diagnostics::RunLogger::Scope> timing;
    if(logger)timing.emplace(logger->scope(QStringLiteral("readUri · ")+uri));
    if (uri.startsWith(QStringLiteral("data:"))) {
        bytes = decodeDataUri(uri, error,logger);
        if (!error.isEmpty()) return false;
        if (canonicalPath) canonicalPath->clear();
        return true;
    }
    QString decoded = QUrl::fromPercentEncoding(uri.toUtf8());
    QString path;
    if (!safeFile(baseDir, decoded, path, error)) return false;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return fail(error, QStringLiteral("Cannot read glTF resource %1: %2").arg(path, file.errorString()));
    bytes = file.readAll();
    if (canonicalPath) *canonicalPath = path;
    return true;
}

bool readGltf(const QString& path, GltfData& out, QString& error, diagnostics::RunLogger* logger) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return fail(error, QStringLiteral("Cannot open model %1: %2").arg(path, file.errorString()));
    const QByteArray fileBytes = file.readAll();
    const QString suffix = QFileInfo(path).suffix().toLower();
    QByteArray jsonBytes, glbBinary;
    if (suffix == QStringLiteral("glb")) {
        if (fileBytes.size() < 20 || fileBytes.left(4) != QByteArrayLiteral("glTF"))
            return fail(error, QStringLiteral("Invalid GLB header: %1").arg(path));
        const auto* raw = reinterpret_cast<const uchar*>(fileBytes.constData());
        const quint32 version = qFromLittleEndian<quint32>(raw + 4);
        const quint32 declaredLength = qFromLittleEndian<quint32>(raw + 8);
        if (version != 2 || declaredLength != quint32(fileBytes.size()))
            return fail(error, QStringLiteral("Expected a complete glTF 2.0 GLB: %1").arg(path));
        qsizetype offset = 12;
        while (offset + 8 <= fileBytes.size()) {
            const quint32 length = qFromLittleEndian<quint32>(raw + offset);
            const quint32 type = qFromLittleEndian<quint32>(raw + offset + 4);
            offset += 8;
            if (length > quint32(fileBytes.size() - offset))
                return fail(error, QStringLiteral("Truncated GLB chunk: %1").arg(path));
            const QByteArray chunk = fileBytes.mid(offset, qsizetype(length));
            offset += qsizetype(length);
            if (type == kGlbJson) jsonBytes = chunk;
            else if (type == kGlbBin) glbBinary = chunk;
        }
    } else if (suffix == QStringLiteral("gltf")) {
        jsonBytes = fileBytes;
    } else {
        return fail(error, QStringLiteral("Model must be a .glb or .gltf file: %1").arg(path));
    }
    QJsonParseError parse;
    const QJsonDocument doc = QJsonDocument::fromJson(jsonBytes, &parse);
    if (parse.error != QJsonParseError::NoError || !doc.isObject())
        return fail(error, QStringLiteral("Invalid glTF JSON in %1: %2").arg(path, parse.errorString()));
    out.json = doc.object();
    const auto asset = out.json.value(QStringLiteral("asset")).toObject();
    if (!asset.value(QStringLiteral("version")).toString().startsWith(QStringLiteral("2.")))
        return fail(error, QStringLiteral("Only glTF 2.0 assets are supported."));

    const QJsonArray bufferDocs = out.json.value(QStringLiteral("buffers")).toArray();
    if (bufferDocs.isEmpty()) return fail(error, QStringLiteral("glTF has no buffers."));
    const QString baseDir = QFileInfo(path).absolutePath();
    for (int i = 0; i < bufferDocs.size(); ++i) {
        const auto buffer = bufferDocs[i].toObject();
        QByteArray bytes;
        if (buffer.contains(QStringLiteral("uri"))) {
            if (!readUri(baseDir, buffer.value(QStringLiteral("uri")).toString(), bytes, nullptr, error,logger)) return false;
        } else if (i == 0 && !glbBinary.isEmpty()) {
            bytes = glbBinary;
        } else {
            return fail(error, QStringLiteral("glTF buffer %1 has no available URI or GLB BIN chunk.").arg(i));
        }
        const qint64 declared = qint64(buffer.value(QStringLiteral("byteLength")).toDouble(-1));
        if (declared < 0 || bytes.size() < declared)
            return fail(error, QStringLiteral("glTF buffer %1 is shorter than byteLength.").arg(i));
        out.buffers.push_back(std::move(bytes));
    }
    return true;
}

struct AccessorData { int components = 0; QVector<double> values; };

int componentCount(const QString& type) {
    if (type == QStringLiteral("SCALAR")) return 1;
    if (type == QStringLiteral("VEC2")) return 2;
    if (type == QStringLiteral("VEC3")) return 3;
    if (type == QStringLiteral("VEC4")) return 4;
    if (type == QStringLiteral("MAT2")) return 4;
    if (type == QStringLiteral("MAT3")) return 9;
    if (type == QStringLiteral("MAT4")) return 16;
    return 0;
}

int componentSize(int type) {
    switch (type) {
    case 5120: case 5121: return 1;
    case 5122: case 5123: return 2;
    case 5125: case 5126: return 4;
    default: return 0;
    }
}

double readComponent(const char* ptr, int type, bool normalized) {
    const auto* b = reinterpret_cast<const uchar*>(ptr);
    switch (type) {
    case 5120: { const qint8 v = qint8(*b); return normalized ? std::max(-1.0, double(v) / 127.0) : double(v); }
    case 5121: { const quint8 v = *b; return normalized ? double(v) / 255.0 : double(v); }
    case 5122: { const qint16 v = qFromLittleEndian<qint16>(b); return normalized ? std::max(-1.0, double(v) / 32767.0) : double(v); }
    case 5123: { const quint16 v = qFromLittleEndian<quint16>(b); return normalized ? double(v) / 65535.0 : double(v); }
    case 5125: return double(qFromLittleEndian<quint32>(b));
    case 5126: {
        const quint32 bits = qFromLittleEndian<quint32>(b);
        float value = 0.f;
        std::memcpy(&value, &bits, sizeof(value));
        return double(value);
    }
    default: return std::numeric_limits<double>::quiet_NaN();
    }
}

bool accessor(const GltfData& gltf, int index, AccessorData& result, QString& error,
              diagnostics::RunLogger* logger = nullptr) {
    std::optional<diagnostics::RunLogger::Scope> timing;
    if(logger)timing.emplace(logger->scope(QStringLiteral("accessor · index %1").arg(index)));
    const auto accessors = gltf.json.value(QStringLiteral("accessors")).toArray();
    const auto views = gltf.json.value(QStringLiteral("bufferViews")).toArray();
    if (index < 0 || index >= accessors.size()) return fail(error, QStringLiteral("Accessor index out of range."));
    const auto a = accessors[index].toObject();
    if (a.contains(QStringLiteral("sparse")))
        return fail(error, QStringLiteral("Sparse glTF accessors are not supported by this asset loader."));
    const int viewIndex = a.value(QStringLiteral("bufferView")).toInt(-1);
    const int type = a.value(QStringLiteral("componentType")).toInt(-1);
    const int components = componentCount(a.value(QStringLiteral("type")).toString());
    const int bytesPerComponent = componentSize(type);
    const int count = a.value(QStringLiteral("count")).toInt(-1);
    if (viewIndex < 0 || viewIndex >= views.size() || components <= 0 || bytesPerComponent <= 0 || count < 0)
        return fail(error, QStringLiteral("Invalid glTF accessor %1.").arg(index));
    const auto view = views[viewIndex].toObject();
    const int bufferIndex = view.value(QStringLiteral("buffer")).toInt(0);
    if (bufferIndex < 0 || bufferIndex >= gltf.buffers.size())
        return fail(error, QStringLiteral("Invalid buffer index in accessor %1.").arg(index));
    const QByteArray& buffer = gltf.buffers[bufferIndex];
    const qint64 viewOffset = qint64(view.value(QStringLiteral("byteOffset")).toDouble(0));
    const qint64 viewLength = qint64(view.value(QStringLiteral("byteLength")).toDouble(-1));
    const qint64 accessorOffset = qint64(a.value(QStringLiteral("byteOffset")).toDouble(0));
    const qint64 elementSize = qint64(components) * bytesPerComponent;
    const qint64 stride = qint64(view.value(QStringLiteral("byteStride")).toDouble(elementSize));
    if (viewLength < 0 || stride < elementSize || accessorOffset < 0 ||
        qint64(count) * qint64(components) > std::numeric_limits<int>::max())
        return fail(error, QStringLiteral("Invalid range or stride in accessor %1.").arg(index));
    const qint64 finalByte = count == 0 ? accessorOffset : accessorOffset + qint64(count - 1) * stride + elementSize;
    if (finalByte > viewLength || viewOffset + finalByte > buffer.size())
        return fail(error, QStringLiteral("Accessor %1 reads past its bufferView.").arg(index));
    result.components = components;
    result.values.resize(count * components);
    const bool normalized = a.value(QStringLiteral("normalized")).toBool(false);
    for (int row = 0; row < count; ++row) {
        const qint64 rowOffset = viewOffset + accessorOffset + qint64(row) * stride;
        for (int col = 0; col < components; ++col) {
            const double value = readComponent(buffer.constData() + rowOffset + qint64(col) * bytesPerComponent, type, normalized);
            if (!std::isfinite(value)) return fail(error, QStringLiteral("Non-finite value in accessor %1.").arg(index));
            result.values[row * components + col] = value;
        }
    }
    return true;
}

Matrix4 identity() { return {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1}; }

Matrix4 multiply(const Matrix4& a, const Matrix4& b) {
    Matrix4 out{};
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row)
            for (int k = 0; k < 4; ++k)
                out[column * 4 + row] += a[k * 4 + row] * b[column * 4 + k];
    return out;
}

Matrix4 nodeMatrix(const QJsonObject& node, QString& error) {
    const auto matrixValue = node.value(QStringLiteral("matrix"));
    if (matrixValue.isArray()) {
        Matrix4 out{};
        float values[16]{};
        if (!jsonVec(matrixValue, 16, values, error, QStringLiteral("node matrix"))) return identity();
        std::copy(values, values + 16, out.begin());
        return out;
    }
    float t[3]{0,0,0}, s[3]{1,1,1}, q[4]{0,0,0,1};
    if (node.contains(QStringLiteral("translation")) &&
        !jsonVec(node.value(QStringLiteral("translation")), 3, t, error, QStringLiteral("translation"))) return identity();
    if (node.contains(QStringLiteral("scale")) &&
        !jsonVec(node.value(QStringLiteral("scale")), 3, s, error, QStringLiteral("scale"))) return identity();
    if (node.contains(QStringLiteral("rotation")) &&
        !jsonVec(node.value(QStringLiteral("rotation")), 4, q, error, QStringLiteral("rotation"))) return identity();
    const double qlen = std::sqrt(double(q[0])*q[0] + double(q[1])*q[1] + double(q[2])*q[2] + double(q[3])*q[3]);
    if (!(qlen > 0.0)) { error = QStringLiteral("Zero-length glTF node quaternion."); return identity(); }
    for (float& value : q) value = float(value / qlen);
    const float x=q[0], y=q[1], z=q[2], w=q[3];
    Matrix4 m = {
        (1-2*(y*y+z*z))*s[0], (2*(x*y+z*w))*s[0], (2*(x*z-y*w))*s[0], 0,
        (2*(x*y-z*w))*s[1], (1-2*(x*x+z*z))*s[1], (2*(y*z+x*w))*s[1], 0,
        (2*(x*z+y*w))*s[2], (2*(y*z-x*w))*s[2], (1-2*(x*x+y*y))*s[2], 0,
        t[0], t[1], t[2], 1
    };
    return m;
}

Vec3 transformPoint(const Matrix4& m, Vec3 p) {
    const float w = m[3]*p.x + m[7]*p.y + m[11]*p.z + m[15];
    const float invW = (std::abs(w) > 1e-12f && std::abs(w - 1.f) > 1e-6f) ? 1.f / w : 1.f;
    return {(m[0]*p.x+m[4]*p.y+m[8]*p.z+m[12])*invW,
            (m[1]*p.x+m[5]*p.y+m[9]*p.z+m[13])*invW,
            (m[2]*p.x+m[6]*p.y+m[10]*p.z+m[14])*invW};
}

Vec3 transformNormal(const Matrix4& m, Vec3 v) {
    const float a=m[0], b=m[4], c=m[8], d=m[1], e=m[5], f=m[9], g=m[2], h=m[6], i=m[10];
    const float A=e*i-f*h, B=f*g-d*i, C=d*h-e*g;
    const float D=c*h-b*i, E=a*i-c*g, F=b*g-a*h;
    const float G=b*f-c*e, H=c*d-a*f, I=a*e-b*d;
    Vec3 n{A*v.x+D*v.y+G*v.z, B*v.x+E*v.y+H*v.z, C*v.x+F*v.y+I*v.z};
    const float length = std::sqrt(n.x*n.x+n.y*n.y+n.z*n.z);
    if (length > 1e-12f) { n.x/=length; n.y/=length; n.z/=length; }
    return n;
}

TextureReference textureReference(const QJsonObject& object) {
    TextureReference result;
    result.texture = object.value(QStringLiteral("index")).toInt(-1);
    result.texCoord = object.value(QStringLiteral("texCoord")).toInt(0);
    result.extensions = object.value(QStringLiteral("extensions")).toObject();
    return result;
}

bool loadTextures(const GltfData& gltf, const QString& modelDir, Scene& scene, QString& error,
                  diagnostics::RunLogger* logger) {
    const auto images = gltf.json.value(QStringLiteral("images")).toArray();
    const auto textures = gltf.json.value(QStringLiteral("textures")).toArray();
    const auto views = gltf.json.value(QStringLiteral("bufferViews")).toArray();
    QVector<QImage> decodedImages(images.size());
    QVector<QString> imagePaths(images.size());
    QVector<QString> imageNames(images.size());
    for (int i = 0; i < images.size(); ++i) {
        std::optional<diagnostics::RunLogger::Scope> imageTiming;
        if(logger)imageTiming.emplace(logger->scope(QStringLiteral("image decode · %1/%2").arg(i+1).arg(images.size())));
        const auto imageDoc = images[i].toObject();
        QByteArray bytes;
        QString sourcePath;
        if (imageDoc.contains(QStringLiteral("uri"))) {
            if (!readUri(modelDir, imageDoc.value(QStringLiteral("uri")).toString(), bytes, &sourcePath, error,logger)) return false;
        } else if (imageDoc.contains(QStringLiteral("bufferView"))) {
            const int vi = imageDoc.value(QStringLiteral("bufferView")).toInt(-1);
            if (vi < 0 || vi >= views.size()) return fail(error, QStringLiteral("Invalid embedded image bufferView."));
            const auto view = views[vi].toObject();
            const int bi = view.value(QStringLiteral("buffer")).toInt(0);
            if (bi < 0 || bi >= gltf.buffers.size()) return fail(error, QStringLiteral("Invalid image buffer index."));
            const qint64 start = qint64(view.value(QStringLiteral("byteOffset")).toDouble(0));
            const qint64 length = qint64(view.value(QStringLiteral("byteLength")).toDouble(-1));
            if (start < 0 || length <= 0 || start + length > gltf.buffers[bi].size())
                return fail(error, QStringLiteral("Embedded image exceeds its bufferView."));
            bytes = gltf.buffers[bi].mid(start, length);
        } else {
            return fail(error, QStringLiteral("glTF image has neither URI nor bufferView."));
        }
        QImage decoded = QImage::fromData(bytes);
        if (decoded.isNull()) return fail(error, QStringLiteral("Cannot decode glTF image %1.").arg(i));
        decodedImages[i] = std::move(decoded);
        imagePaths[i] = sourcePath;
        imageNames[i] = imageDoc.value(QStringLiteral("name")).toString(QStringLiteral("image_%1").arg(i));
    }
    for (int i = 0; i < textures.size(); ++i) {
        const auto textureDoc = textures[i].toObject();
        const int source = textureDoc.value(QStringLiteral("source")).toInt(-1);
        if (source < 0 || source >= decodedImages.size())
            return fail(error, QStringLiteral("Invalid glTF texture source index %1.").arg(i));
        Texture tex;
        tex.name = textureDoc.value(QStringLiteral("name")).toString(imageNames[source]);
        tex.sourcePath = imagePaths[source];
        tex.image = decodedImages[source];
        const int sampler = textureDoc.value(QStringLiteral("sampler")).toInt(-1);
        if (sampler >= 0) {
            const auto samplers = gltf.json.value(QStringLiteral("samplers")).toArray();
            if (sampler >= samplers.size()) return fail(error, QStringLiteral("Invalid glTF sampler index."));
            tex.sampler = samplers[sampler].toObject();
        }
        scene.textures.push_back(std::move(tex));
    }
    return true;
}

bool loadMaterials(const GltfData& gltf, Scene& scene, QString& error) {
    const auto docs = gltf.json.value(QStringLiteral("materials")).toArray();
    for (int i = 0; i < docs.size(); ++i) {
        const auto m = docs[i].toObject();
        Material material;
        material.name = m.value(QStringLiteral("name")).toString(QStringLiteral("material_%1").arg(i));
        material.alphaMode = m.value(QStringLiteral("alphaMode")).toString(QStringLiteral("OPAQUE"));
        if (material.alphaMode != QStringLiteral("OPAQUE") && material.alphaMode != QStringLiteral("MASK") && material.alphaMode != QStringLiteral("BLEND"))
            return fail(error, QStringLiteral("Unsupported alphaMode on material %1.").arg(material.name));
        material.alphaCutoff = finiteFloat(m.value(QStringLiteral("alphaCutoff")).toDouble(0.5), 0.5f);
        material.doubleSided = m.value(QStringLiteral("doubleSided")).toBool(false);
        material.extensions = m.value(QStringLiteral("extensions")).toObject();
        const auto pbr = m.value(QStringLiteral("pbrMetallicRoughness")).toObject();
        float base[4]{1,1,1,1};
        if (pbr.contains(QStringLiteral("baseColorFactor")) &&
            !jsonVec(pbr.value(QStringLiteral("baseColorFactor")), 4, base, error, QStringLiteral("baseColorFactor"))) return false;
        material.baseColor = {base[0], base[1], base[2], base[3]};
        material.metallic = finiteFloat(pbr.value(QStringLiteral("metallicFactor")).toDouble(1.0), 1.f);
        material.roughness = finiteFloat(pbr.value(QStringLiteral("roughnessFactor")).toDouble(1.0), 1.f);
        float emissive[3]{0,0,0};
        if (m.contains(QStringLiteral("emissiveFactor")) &&
            !jsonVec(m.value(QStringLiteral("emissiveFactor")), 3, emissive, error, QStringLiteral("emissiveFactor"))) return false;
        material.emissive = {emissive[0], emissive[1], emissive[2]};
        material.baseColorTexture = textureReference(pbr.value(QStringLiteral("baseColorTexture")).toObject());
        material.metallicRoughnessTexture = textureReference(pbr.value(QStringLiteral("metallicRoughnessTexture")).toObject());
        material.normalTexture = textureReference(m.value(QStringLiteral("normalTexture")).toObject());
        material.occlusionTexture = textureReference(m.value(QStringLiteral("occlusionTexture")).toObject());
        material.emissiveTexture = textureReference(m.value(QStringLiteral("emissiveTexture")).toObject());
        material.normalScale = finiteFloat(m.value(QStringLiteral("normalTexture")).toObject().value(QStringLiteral("scale")).toDouble(1.0), 1.f);
        material.occlusionStrength = finiteFloat(m.value(QStringLiteral("occlusionTexture")).toObject().value(QStringLiteral("strength")).toDouble(1.0), 1.f);
        if (material.metallic < 0 || material.metallic > 1 || material.roughness < 0 || material.roughness > 1 ||
            material.alphaCutoff < 0 || material.alphaCutoff > 1)
            return fail(error, QStringLiteral("Invalid PBR factor on material %1.").arg(material.name));
        scene.materials.push_back(std::move(material));
    }
    return true;
}

void expandBounds(Bounds& bounds, Vec3 p) {
    if (!bounds.valid) { bounds.minimum = bounds.maximum = p; bounds.valid = true; return; }
    bounds.minimum = {std::min(bounds.minimum.x,p.x), std::min(bounds.minimum.y,p.y), std::min(bounds.minimum.z,p.z)};
    bounds.maximum = {std::max(bounds.maximum.x,p.x), std::max(bounds.maximum.y,p.y), std::max(bounds.maximum.z,p.z)};
}

bool readAttribute(const GltfData& gltf, const QJsonObject& attrs, const QString& name,
                   int expected, int vertexCount, QVector<double>& values, QString& error,
                   bool required = false, diagnostics::RunLogger* logger = nullptr) {
    std::optional<diagnostics::RunLogger::Scope> timing;
    if(logger)timing.emplace(logger->scope(QStringLiteral("readAttribute · ")+name));
    if (!attrs.contains(name)) {
        if (required) return fail(error, QStringLiteral("glTF primitive lacks required %1 attribute.").arg(name));
        values.clear();
        return true;
    }
    AccessorData data;
    if (!accessor(gltf, attrs.value(name).toInt(-1), data, error,logger)) return false;
    if (data.components != expected || data.values.size() / expected != vertexCount)
        return fail(error, QStringLiteral("Invalid %1 attribute dimensions.").arg(name));
    values = std::move(data.values);
    return true;
}

bool appendPrimitive(const GltfData& gltf, int meshIndex, const QJsonObject& mesh,
                     const QJsonObject& primitiveDoc, const QJsonObject& node,
                     const Matrix4& world, int materialOffset, Scene& scene, QString& error,
                     diagnostics::RunLogger* logger) {
    std::optional<diagnostics::RunLogger::Scope> timing;
    if(logger)timing.emplace(logger->scope(QStringLiteral("appendPrimitive · %1").arg(mesh.value(QStringLiteral("name")).toString(QStringLiteral("mesh_%1").arg(meshIndex)))));
    if (primitiveDoc.value(QStringLiteral("mode")).toInt(4) != 4)
        return fail(error, QStringLiteral("Only triangle-list glTF primitives are supported."));
    const auto attrs = primitiveDoc.value(QStringLiteral("attributes")).toObject();
    AccessorData pos;
    if (!accessor(gltf, attrs.value(QStringLiteral("POSITION")).toInt(-1), pos, error,logger)) return false;
    if (pos.components != 3 || pos.values.size() % 3)
        return fail(error, QStringLiteral("Invalid POSITION accessor."));
    const int vertexCount = pos.values.size() / 3;
    QVector<double> normals, uv, tangents, colors;
    if (!readAttribute(gltf, attrs, QStringLiteral("NORMAL"), 3, vertexCount, normals, error,false,logger)) return false;
    if (!readAttribute(gltf, attrs, QStringLiteral("TEXCOORD_0"), 2, vertexCount, uv, error,false,logger)) return false;
    if (!readAttribute(gltf, attrs, QStringLiteral("TANGENT"), 4, vertexCount, tangents, error,false,logger)) return false;
    if (attrs.contains(QStringLiteral("COLOR_0"))) {
        AccessorData colorData;
        if (!accessor(gltf, attrs.value(QStringLiteral("COLOR_0")).toInt(-1), colorData, error,logger)) return false;
        if ((colorData.components != 3 && colorData.components != 4) ||
            colorData.values.size() / colorData.components != vertexCount)
            return fail(error, QStringLiteral("Invalid COLOR_0 attribute dimensions."));
        colors.reserve(vertexCount * 4);
        for (int v = 0; v < vertexCount; ++v) {
            colors << colorData.values[v * colorData.components]
                   << colorData.values[v * colorData.components + 1]
                   << colorData.values[v * colorData.components + 2]
                   << (colorData.components == 4 ? colorData.values[v * 4 + 3] : 1.0);
        }
    }
    Primitive out;
    out.meshName = mesh.value(QStringLiteral("name")).toString(QStringLiteral("mesh_%1").arg(meshIndex));
    out.nodeName = node.value(QStringLiteral("name")).toString(out.meshName);
    out.worldTransform = world;
    out.extensions = primitiveDoc.value(QStringLiteral("extensions")).toObject();
    out.material = primitiveDoc.value(QStringLiteral("material")).toInt(-1);
    if (out.material < 0) {
        if (scene.materials.isEmpty()) scene.materials.push_back(Material{QStringLiteral("default")});
        out.material = 0;
    }
    out.material += materialOffset;
    if (out.material >= scene.materials.size())
        return fail(error, QStringLiteral("Primitive material index is out of range."));
    out.vertices.resize(vertexCount);
    for (int i=0;i<vertexCount;++i) {
        Vertex v;
        v.position = {finiteFloat(pos.values[i*3]), finiteFloat(pos.values[i*3+1]), finiteFloat(pos.values[i*3+2])};
        if (!normals.isEmpty()) v.normal = {finiteFloat(normals[i*3]), finiteFloat(normals[i*3+1]), finiteFloat(normals[i*3+2])};
        if (!uv.isEmpty()) v.uv0 = {finiteFloat(uv[i*2]), finiteFloat(uv[i*2+1])};
        if (!tangents.isEmpty()) v.tangent = {finiteFloat(tangents[i*4]), finiteFloat(tangents[i*4+1]), finiteFloat(tangents[i*4+2]), finiteFloat(tangents[i*4+3])};
        if (!colors.isEmpty()) v.color = {finiteFloat(colors[i*4]), finiteFloat(colors[i*4+1]), finiteFloat(colors[i*4+2]), finiteFloat(colors[i*4+3],1.f)};
        out.vertices[i] = v;
        expandBounds(scene.bounds, transformPoint(world, v.position));
    }
    if (primitiveDoc.contains(QStringLiteral("indices"))) {
        AccessorData indexData;
        if (!accessor(gltf, primitiveDoc.value(QStringLiteral("indices")).toInt(-1), indexData, error,logger)) return false;
        if (indexData.components != 1 || indexData.values.size() % 3)
            return fail(error, QStringLiteral("Invalid triangle index accessor."));
        out.indices.reserve(size_t(indexData.values.size()));
        for (double value : indexData.values) {
            if (value < 0 || value >= vertexCount || value > double(std::numeric_limits<std::uint32_t>::max()))
                return fail(error, QStringLiteral("Primitive index is outside its vertex array."));
            out.indices.push_back(std::uint32_t(value));
        }
    } else {
        if (vertexCount % 3) return fail(error, QStringLiteral("Non-indexed triangle primitive has a non-triangle vertex count."));
        out.indices.resize(size_t(vertexCount));
        for (int i=0;i<vertexCount;++i) out.indices[size_t(i)] = std::uint32_t(i);
    }
    scene.triangleCount += out.indices.size() / 3;
    scene.primitives.push_back(std::move(out));
    return true;
}

bool traverseNode(const GltfData& gltf, int index, const Matrix4& parent,
                  int materialOffset, QVector<bool>& active, Scene& scene, QString& error,
                  diagnostics::RunLogger* logger) {
    const auto nodes = gltf.json.value(QStringLiteral("nodes")).toArray();
    const auto meshes = gltf.json.value(QStringLiteral("meshes")).toArray();
    if (index < 0 || index >= nodes.size()) return fail(error, QStringLiteral("glTF node index out of range."));
    if (active[index]) return fail(error, QStringLiteral("Cycle in glTF node hierarchy."));
    std::optional<diagnostics::RunLogger::Scope> timing;
    if(logger){const QString label=nodes[index].toObject().value(QStringLiteral("name")).toString(QStringLiteral("node_%1").arg(index));timing.emplace(logger->scope(QStringLiteral("traverseNode · ")+label));}
    active[index] = true;
    const auto node = nodes[index].toObject();
    QString transformError;
    const Matrix4 local = nodeMatrix(node, transformError);
    if (!transformError.isEmpty()) return fail(error, transformError);
    const Matrix4 world = multiply(parent, local);
    if (node.contains(QStringLiteral("mesh"))) {
        const int meshIndex = node.value(QStringLiteral("mesh")).toInt(-1);
        if (meshIndex < 0 || meshIndex >= meshes.size()) return fail(error, QStringLiteral("glTF node references an invalid mesh."));
        const auto mesh = meshes[meshIndex].toObject();
        const auto primitives = mesh.value(QStringLiteral("primitives")).toArray();
        for (const auto& value : primitives)
            if (!appendPrimitive(gltf, meshIndex, mesh, value.toObject(), node, world, materialOffset, scene, error,logger)) return false;
    }
    for (const auto& child : node.value(QStringLiteral("children")).toArray())
        if (!traverseNode(gltf, child.toInt(-1), world, materialOffset, active, scene, error,logger)) return false;
    active[index] = false;
    return true;
}

bool loadModel(const QString& path, Scene& scene, QString& error, diagnostics::RunLogger* logger) {
    std::optional<diagnostics::RunLogger::Scope> modelTiming;
    if(logger)modelTiming.emplace(logger->scope(QStringLiteral("loadModel · ")+QFileInfo(path).fileName()));
    GltfData gltf;
    {
        std::optional<diagnostics::RunLogger::Scope> timing;
        if(logger)timing.emplace(logger->scope(QStringLiteral("readGltf · JSON, GLB et buffers BIN")));
        if (!readGltf(path, gltf, error,logger)) return false;
        if(logger)logger->note(QStringLiteral("GLB BIN extrait par copie ; aucun décodage Draco/Meshopt demandé par cet asset."));
    }
    const QString modelDir = QFileInfo(path).absolutePath();
    const int textureOffset = scene.textures.size();
    const int materialOffset = scene.materials.size();
    {
        std::optional<diagnostics::RunLogger::Scope> timing;
        if(logger)timing.emplace(logger->scope(QStringLiteral("loadTextures · décodage QImage")));
        if(!loadTextures(gltf,modelDir,scene,error,logger))return false;
        if(logger)logger->note(QStringLiteral("Textures décodées : %1").arg(scene.textures.size()-textureOffset));
    }
    {
        std::optional<diagnostics::RunLogger::Scope> timing;
        if(logger)timing.emplace(logger->scope(QStringLiteral("loadMaterials · PBR et alpha")));
        if(!loadMaterials(gltf,scene,error))return false;
        if(logger)logger->note(QStringLiteral("Matériaux chargés : %1").arg(scene.materials.size()-materialOffset));
    }
    if (scene.materials.size() == materialOffset)
        scene.materials.push_back(Material{QStringLiteral("default")});
    for (int i = materialOffset; i < scene.materials.size(); ++i) {
        auto& material = scene.materials[i];
        TextureReference* refs[] = {&material.baseColorTexture, &material.metallicRoughnessTexture,
            &material.normalTexture, &material.occlusionTexture, &material.emissiveTexture};
        for (auto* ref : refs) if (ref->texture >= 0) ref->texture += textureOffset;
    }
    const auto nodes = gltf.json.value(QStringLiteral("nodes")).toArray();
    const auto scenes = gltf.json.value(QStringLiteral("scenes")).toArray();
    const int activeScene = gltf.json.value(QStringLiteral("scene")).toInt(0);
    QJsonArray roots;
    if (!scenes.isEmpty()) {
        if (activeScene < 0 || activeScene >= scenes.size()) return fail(error, QStringLiteral("Invalid active glTF scene."));
        roots = scenes[activeScene].toObject().value(QStringLiteral("nodes")).toArray();
    } else {
        QVector<bool> isChild(nodes.size(), false);
        for (const auto& value : nodes)
            for (const auto& child : value.toObject().value(QStringLiteral("children")).toArray()) {
                const int id = child.toInt(-1);
                if (id < 0 || id >= nodes.size()) return fail(error, QStringLiteral("Invalid child node reference."));
                isChild[id] = true;
            }
        for (int i=0;i<nodes.size();++i) if (!isChild[i]) roots.append(i);
    }
    QVector<bool> active(nodes.size(), false);
    {
        std::optional<diagnostics::RunLogger::Scope> timing;
        if(logger)timing.emplace(logger->scope(QStringLiteral("traverseNode · hiérarchie et primitives")));
        for (const auto& root : roots)
            if (!traverseNode(gltf, root.toInt(-1), identity(), materialOffset, active, scene, error,logger)) return false;
    }
    if (scene.primitives.isEmpty() || !scene.triangleCount)
        return fail(error, QStringLiteral("The glTF scene contains no triangle primitives."));
    for (const auto& material : scene.materials) {
        const TextureReference refs[] = {material.baseColorTexture, material.metallicRoughnessTexture,
            material.normalTexture, material.occlusionTexture, material.emissiveTexture};
        for (const auto& ref : refs)
            if (ref.texture >= scene.textures.size())
                return fail(error, QStringLiteral("Material %1 references a missing texture.").arg(material.name));
    }
    return true;
}
} // namespace

LoadResult loadScene(const QString& manifestPath, diagnostics::RunLogger* logger) {
    std::optional<diagnostics::RunLogger::Scope> sceneTiming;
    if(logger)sceneTiming.emplace(logger->scope(QStringLiteral("scene::loadScene · ")+manifestPath));
    LoadResult result;
    const QFileInfo manifestInfo(manifestPath);
    const QString root = manifestInfo.absolutePath();
    QJsonObject manifest;
    {
        std::optional<diagnostics::RunLogger::Scope> timing;
        if(logger)timing.emplace(logger->scope(QStringLiteral("manifest · ouverture et lecture JSON")));
        if (!readJsonObject(manifestInfo.absoluteFilePath(), manifest, result.error)) return result;
    }
    if (manifest.value(QStringLiteral("format")).toString() != QStringLiteral("lumatrace-scene-v1")) {
        result.error = QStringLiteral("Expected lumatrace-scene-v1 manifest.");
        return result;
    }
    const auto objects = manifest.value(QStringLiteral("objects")).toArray();
    if (objects.isEmpty()) { result.error = QStringLiteral("Scene has no objects."); return result; }
    result.scene.name = manifest.value(QStringLiteral("name")).toString();
    result.scene.rootPath = QFileInfo(root).canonicalFilePath();
    result.scene.environment = manifest.value(QStringLiteral("environment")).toObject();
    for (const auto& objectValue : objects) {
        const auto object = objectValue.toObject();
        if (object.value(QStringLiteral("type")).toString() != QStringLiteral("gltf2-model")) {
            result.error = QStringLiteral("Unsupported scene object type: %1").arg(object.value(QStringLiteral("type")).toString());
            return result;
        }
        QString modelPath;
        if (!safeFile(root, object.value(QStringLiteral("file")).toString(), modelPath, result.error)) return result;
        if (result.scene.modelPath.isEmpty()) result.scene.modelPath = modelPath;
        if (!loadModel(modelPath, result.scene, result.error,logger)) {
            result.scene = Scene{}; // discard partial state; preserve transactional load behavior
            return result;
        }
    }
    const auto materialOverrides = manifest.value(QStringLiteral("materialOverrides")).toArray();
    for (int overrideIndex = 0; overrideIndex < materialOverrides.size(); ++overrideIndex) {
        const auto overrideDoc = materialOverrides[overrideIndex].toObject();
        const QString materialName = overrideDoc.value(QStringLiteral("material")).toString();
        const QString textureRelativePath = overrideDoc.value(QStringLiteral("baseColorTexture")).toString();
        if (materialName.isEmpty() || textureRelativePath.isEmpty()) {
            result.error = QStringLiteral("Material override %1 requires material and baseColorTexture.").arg(overrideIndex);
            return result;
        }
        if (overrideDoc.value(QStringLiteral("texCoord")).toInt(0) != 0) {
            result.error = QStringLiteral("Material override %1 uses unsupported texCoord; LumaTrace currently reads TEXCOORD_0.").arg(overrideIndex);
            return result;
        }
        int materialIndex = -1;
        for (int i = 0; i < result.scene.materials.size(); ++i) {
            if (result.scene.materials[i].name == materialName) { materialIndex = i; break; }
        }
        if (materialIndex < 0) {
            result.error = QStringLiteral("Material override target not found: %1.").arg(materialName);
            return result;
        }
        QString texturePath;
        if (!safeFile(root, textureRelativePath, texturePath, result.error)) return result;
        QImage image(texturePath);
        if (image.isNull()) {
            result.error = QStringLiteral("Cannot decode material override texture: %1.").arg(textureRelativePath);
            return result;
        }
        Texture texture;
        texture.name = QStringLiteral("%1_baseColor_override").arg(materialName);
        texture.sourcePath = texturePath;
        texture.image = std::move(image);
        const int textureIndex = result.scene.textures.size();
        result.scene.textures.push_back(std::move(texture));
        auto& material = result.scene.materials[materialIndex];
        material.baseColorTexture.texture = textureIndex;
        material.baseColorTexture.texCoord = 0;
        float factor[4]{1.f, 1.f, 1.f, 1.f};
        if (overrideDoc.contains(QStringLiteral("baseColorFactor")) &&
            !jsonVec(overrideDoc.value(QStringLiteral("baseColorFactor")), 4, factor, result.error,
                     QStringLiteral("materialOverrides[%1].baseColorFactor").arg(overrideIndex))) return result;
        for (float component : factor) if (component < 0.f || component > 1.f) {
            result.error = QStringLiteral("Material override %1 baseColorFactor components must be in [0, 1].").arg(overrideIndex);
            return result;
        }
        material.baseColor = {factor[0], factor[1], factor[2], factor[3]};
        if (logger) logger->note(QStringLiteral("Override texture · matériau %1 → %2").arg(materialName, textureRelativePath));
    }
    return result;
}

} // namespace lumatrace::scene
