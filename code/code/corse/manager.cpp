#include "scene_manager.h"
#include "bary.h"
#include "texture.h"
#include "geometry_shader.h"

#define NDCX_TO_RASTER(ndc_x, width) (((ndc_x + 1.0f) * (width >> 1)))
#define NDCY_TO_RASTER(ndc_y, height) (((1.0f - ndc_y) * (height >> 1)))
void denormolize(int width, int height, Vertex& v){
    v.pos.x = v.pos.x * v.invW;
    v.pos.y = v.pos.y * v.invW;

    v.pos.x = NDCX_TO_RASTER(v.pos.x, width);
    v.pos.y = NDCY_TO_RASTER(v.pos.y, height);
}


void SceneManager::init()
{
    models.push_back(new Light(Light::light_type::ambient));
    pixel_shader = std::make_shared<ColorShader>();
    vertex_shader = std::make_shared<VertexShader>();
    geom_shader = std::make_shared<GeometryShader>();
    render_all();
}

void SceneManager::render()
{
    this->render_all();
}

void SceneManager::render_all()
{

    img.fill(Qt::black);

    for (auto& vec: depthBuffer)
        std::fill(vec.begin(), vec.end(), std::numeric_limits<float>::max());

    for (auto& model: models)
    {
        if (!model->isObject())
        {
            Light* l = dynamic_cast<Light*>(model);
            if (l->t == Light::light_type::ambient) continue;
        }
        if (model->has_texture)
            pixel_shader = std::make_shared<TextureShader>(model->texture);
        else
            pixel_shader = std::make_shared<ColorShader>();
        rasterize(*model);
    }

    show();
}

bool SceneManager::backfaceCulling(const Vertex &a, const Vertex &b, const Vertex &c)
{
    auto cam = camers[curr_camera];

    auto face_normal = Vec3f::cross(b.pos - a.pos, c.pos - a.pos);

    auto res1 = Vec3f::dot(face_normal, a.pos - cam.position);
    auto res2 = Vec3f::dot(face_normal, b.pos - cam.position);
    auto res3 = Vec3f::dot(face_normal, c.pos - cam.position);

    if ((res1 > 0) && (res2 > 0) && (res3 > 0))
        return true;
    return false;
}

bool SceneManager::clip(const Vertex& v)
{
    const float w = 1 / v.invW;
    return (v.pos.x > -w || fabs(v.pos.x + w) < eps) &&
           (v.pos.x < w || fabs(v.pos.x - w) < eps) &&
           (v.pos.y > -w || fabs(v.pos.y + w) < eps) &&
           (v.pos.y < w || fabs(v.pos.y - w) < eps) &&
           (v.pos.z > 0.f || fabs(v.pos.z) < eps) &&
           (v.pos.z < w || fabs(v.pos.z - w) < eps);
}


void SceneManager::rasterize(Model& model)
{
    auto cam = camers[curr_camera];
    auto rotation_matrix = model.rotation_matrix;
    auto objToWorld = model.objToWorld();
    auto viewMatrix = cam.viewMatrix();
    auto projMatrix = cam.projectionMatrix;

    for (auto& face: model.faces)
    {

        auto a = vertex_shader->shade(face.a, rotation_matrix, objToWorld, cam);
        auto b = vertex_shader->shade(face.b, rotation_matrix, objToWorld, cam);
        auto c = vertex_shader->shade(face.c, rotation_matrix, objToWorld, cam);

        if (backfaceCulling(a, b, c))
            continue;

        a = geom_shader->shade(a, projMatrix, viewMatrix);
        b = geom_shader->shade(b, projMatrix, viewMatrix);
        c = geom_shader->shade(c, projMatrix, viewMatrix);

        rasterBarTriangle(a, b, c);
    }
}

#define Min(val1, val2) std::min(val1, val2)
#define Max(val1, val2) std::max(val1, val2)
// заполнение треугольника с использованием барицентрических координат
void SceneManager::rasterBarTriangle(Vertex p1_, Vertex p2_, Vertex p3_)
{
    if (!clip(p1_) && !clip(p2_) && !clip(p3_))
        return;

    denormolize(width, height, p1_);
    denormolize(width, height, p2_);
    denormolize(width, height, p3_);

    auto p1 = p1_.pos;
    auto p2 = p2_.pos;
    auto p3 = p3_.pos;

    float sx = std::floor(Min(Min(p1.x, p2.x), p3.x));
    float ex = std::ceil(Max(Max(p1.x, p2.x), p3.x));

    float sy = std::floor(Min(Min(p1.y, p2.y), p3.y));
    float ey = std::ceil(Max(Max(p1.y, p2.y), p3.y));

    for (int y = static_cast<int>(sy); y < static_cast<int>(ey); y++)
    {
        for (int x = static_cast<int>(sx); x < static_cast<int>(ex); x++)
        {
            Vec3f bary = toBarycentric(p1, p2, p3, Vec3f(static_cast<float>(x), static_cast<float>(y)));
            if ( (bary.x > 0.0f || fabs(bary.x) < eps) && (bary.x < 1.0f || fabs(bary.x - 1.0f) < eps) &&
                 (bary.y > 0.0f || fabs(bary.y) < eps) && (bary.y < 1.0f || fabs(bary.y - 1.0f) < eps) &&
                 (bary.z > 0.0f || fabs(bary.z) < eps) && (bary.z < 1.0f || fabs(bary.z - 1.0f) < eps))
            {
                auto interpolated = baryCentricInterpolation(p1, p2, p3, bary);
                interpolated.x = x;
                interpolated.y = y;
                if (testAndSet(interpolated)){
                    auto pixel_color = pixel_shader->shade(p1_, p2_, p3_, bary) * 255.f;
                    img.setPixelColor(x, y, qRgb(pixel_color.x, pixel_color.y, pixel_color.z));
                }
            }
        }
    }

}

bool SceneManager::testAndSet(const Vec3f& p)
{
    int x = std::round(p.x), y = std::round(p.y);
    if (x >= width || y >= height || x < 0 || y < 0)
        return false;
    if (p.z < depthBuffer[x][y] || fabs(p.z - depthBuffer[x][y]) < eps)
    {
        depthBuffer[x][y] = p.z;
        return true;
    }
    return false;
}


void SceneManager::showTracedResult()
{
    for (auto& th: threads)
    {
        th->wait();
        delete th;
    }
    threads.clear();
    this->show();
}

void SceneManager::show()
{
    scene->clear();
    scene->addPixmap(QPixmap::fromImage(img));
}

float check_shift(float curr, float target)
{
    return curr > target ? -1.f : 1.f;
}

void SceneManager::shift(trans_type t, float val)
{
    switch (t)
    {
        case shift_x:
            models[current_model]->shiftX(val);
            break;
        case shift_y:
            models[current_model]->shiftY(val);
            break;
        case shift_z:
            models[current_model]->shiftZ(val);
    }

    render_all();
}

void SceneManager::rotate(trans_type t, float angle)
{
    switch (t)
    {
        case rot_x:
            models[current_model]->rotateX(angle);
            break;
        case rot_y:
            models[current_model]->rotateY(angle);
            break;
        case rot_z:
            models[current_model]->rotateZ(angle);
    }

    render_all();
}

void SceneManager::scale(trans_type t, float factor)
{
    switch (t)
    {
        case scale_x:
            models[current_model]->scaleX(factor);
            break;
        case scale_y:
            models[current_model]->scaleY(factor);
            break;
        case scale_z:
            models[current_model]->scaleZ(factor);
    }

    render_all();
}

void SceneManager::moveCamera(trans_type t, float dist)
{
    auto& cam = camers[curr_camera];
    std::function<void ()> change_func;
    switch (t)
    {
        case shift_x:
            change_func = [&]()
            {
                cam.shiftX(dist);
            };
            break;
        case shift_z:
            change_func = [&]()
            {
                cam.shiftZ(dist);
            };
            break;
        case rot_x:
            change_func = [&]()
            {
                cam.rotateX(dist);
            };
            break;
        case rot_y:
            change_func = [&]()
            {
                cam.rotateY(dist);
            };
            break;
        case up_y:
            change_func = [&]()
            {
                cam.shiftY(dist);
            };
        break;
        case down_y:
            change_func = [&]()
            {
                cam.shiftY(dist);
            };
            break;
        default:
            return;
    }

    change_func();

    render_all();
}

const int cube_n = 512, other_n = 20, pyramid_n = 512;
void SceneManager::uploadModel(std::string name, uint32_t& uid)
{

    const std::map<std::string, std::string> files =
    {
        {"Куб", "C:\\Users\\gimna\\Desktop\\BMSTU\\KG\\Praktika\\Frolov\\programm\\models\\cube.obj"},
        {"Сфера", "C:\\Users\\gimna\\Desktop\\BMSTU\\KG\\Praktika\\Frolov\\programm\\models\\less_sphere.obj"},
        {"Пирамида", "C:\\Users\\gimna\\Desktop\\BMSTU\\KG\\Praktika\\Frolov\\programm\\models\\pyr.obj"},
        {"Конус", "C:\\Users\\gimna\\Desktop\\BMSTU\\KG\\Praktika\\Frolov\\programm\\models\\conus_.obj"},
        {"Цилиндр", "C:\\Users\\gimna\\Desktop\\BMSTU\\KG\\Praktika\\Frolov\\programm\\models\\cyl_new.obj"}
    };


    const std::map<std::string, int> n_power =
    {
        {"Куб", cube_n},
        {"Сфера", other_n},
        {"Пирамида", pyramid_n},
        {"Конус", other_n},
        {"Цилиндр", other_n}
    };

    if (!files.count(name))
        return;

    uid = models_index++;
    models.push_back(new Model(files.at(name), uid, n_power.at(name)));

    render_all();
}

void SceneManager::uploadLight(std::string name, uint32_t &uid)
{
    const std::map<std::string, std::string> files =
    {
        {"Точечный источник", "C:\\Users\\gimna\\Desktop\\BMSTU\\KG\\Praktika\\Frolov\\programm\\models\\icosphere.obj"},
        {"Направленный", "C:\\Users\\gimna\\Desktop\\BMSTU\\KG\\Praktika\\Frolov\\programm\\models\\arrow.obj"}
    };

    if (!files.count(name))
        return;

    uid = models_index++;
    if (name == "Точечный источник")
    {
        models.push_back(new Light(Light::light_type::point, {1.f, 1.f, 1.f}, pointLightPosition,
                                   1, {0.f, 0.f, 0.f}, files.at(name), uid, {0.2f, 0.2f, 0.2f}));
    } else if (name == "Направленный")
    {
        models.push_back(new Light(Light::light_type::directional, {1.f, 1.f, 1.f}, directionLightPosition,
                                   1, {0.f, 0.f, -1.f}, files.at(name), uid));
    }

    render_all();
}


void SceneManager::removeModel()
{
    models.erase(models.begin() + current_model);
    render_all();
}

void SceneManager::setCurrentModel(uint32_t uid)
{
    auto it = models.begin();
    int i = 0;
    for (; it <  models.end(); it++, i++)
        if ((*it)->getUid() == uid)
            break;
    current_model = i;
}

void SceneManager::setColor(const Vec3f &color)
{
    models[current_model]->setColor(color);
    render_all();
}

void SceneManager::setTexture(const QImage &img)
{
    models[current_model]->has_texture = true;
    models[current_model]->setColor(Vec3f{1.f, 1.f, 1.f});
    models[current_model]->texture = img;
    render_all();
}

void SceneManager::setFlagTexture(bool flag, const Vec3f& color)
{
    models[current_model]->has_texture = flag;
    models[current_model]->setColor(color);
    render_all();
}

void SceneManager::setSpecular(float val)
{
    models[current_model]->specular = val;
    render_all();
}

void SceneManager::setReflective(float val)
{
    models[current_model]->reflective = val;
    render_all();
}

void SceneManager::setRefraction(float refract)
{
    models[current_model]->refractive = refract;
    render_all();
}

void SceneManager::setIntensity(float intens)
{
    Light* l = dynamic_cast<Light*>(models[current_model]);
    l->color_intensity.x = intens;
    l->color_intensity.y = intens;
    l->color_intensity.z = intens;
    render_all();
}

void SceneManager::setAmbIntensity(float intensity)
{
    for (auto& model: models)
    {
        if (model->isObject()) continue;
        Light* l = dynamic_cast<Light*>(model);
        if (l->t == Light::light_type::ambient)
        {
            l->color_intensity.x = intensity;
            l->color_intensity.y = intensity;
            l->color_intensity.z = intensity;
        }
    }
}
