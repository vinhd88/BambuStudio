#include "AgentBridgeTextHandler.hpp"

#include <cctype>

#include <boost/algorithm/string/case_conv.hpp>
#include <boost/filesystem.hpp>

#include "libslic3r/Emboss.hpp"
#include "libslic3r/EmbossShape.hpp"
#include "libslic3r/Format/STL.hpp"
#include "libslic3r/Format/bbs_3mf.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/TextConfiguration.hpp"
#include "libslic3r/Utils.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/GUI_ObjectList.hpp"
#include "slic3r/GUI/Plater.hpp"

#include "AgentBridgeArgs.hpp"
#include "AgentBridgeGeometryChange.hpp"

using nlohmann::json;
namespace fs = boost::filesystem;

namespace Slic3r { namespace GUI { namespace AgentBridge {

namespace {
// Studio ships this font; it covers Latin, CJK and Vietnamese letters.
std::string default_font() { return resources_dir() + "/fonts/HarmonyOS_Sans_SC_Regular.ttf"; }

// Text mesh lying flat, centred on x/y, from z 0 up to depth. Same steps as EmbossJob's try_create_mesh.
TriangleMesh text_mesh(const std::string &text, const std::string &font_path, double size_mm, double depth_mm)
{
    std::unique_ptr<Emboss::FontFile> file = Emboss::create_font_file(font_path.c_str());
    if (file == nullptr)
        throw BridgeError(400, "invalid_args", "Cannot load the font " + font_path);
    Emboss::FontFileWithCache font(std::move(file));
    FontProp          prop(static_cast<float>(size_mm));
    const double      scale = Emboss::get_text_shape_scale(prop, *font.font_file);
    EmbossShape       shape;
    ExPolygons        shapes = Emboss::text2shapes(shape, font, text.c_str(), prop, scale).expolygons;
    if (shapes.empty())
        throw BridgeError(422, "no_shape", "The font has no letters for this text.");
    auto                     project_z = std::make_unique<Emboss::ProjectZ>(depth_mm / scale);
    Emboss::ProjectTransform project(std::move(project_z), Transform3d(Eigen::Scaling(scale)));
    TriangleMesh             mesh(Emboss::polygons2model(shapes, project));
    const BoundingBoxf3      box = mesh.bounding_box();
    mesh.translate(-float(box.center().x()), -float(box.center().y()), -float(box.min.z()));
    return mesh;
}

std::string file_safe(const std::string &text)
{
    std::string out;
    for (char c : text)
        out += (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_') ? c : '_';
    return "Text-" + out.substr(0, 32);
}
} // namespace

json add_text(const json &args)
{
    Plater           &plater = *wxGetApp().plater();
    Model            &model  = plater.model();
    const std::string text   = string_arg(args, "text");
    if (text.empty() || text.size() > 200)
        throw BridgeError(400, "invalid_args", "text must be 1 to 200 characters");
    const double      size   = optional_number_arg(args, "size_mm", 2, 200).value_or(10);
    const double      depth  = optional_number_arg(args, "depth_mm", 0.2, 50).value_or(1.5);
    const std::string font   = string_arg(args, "font_path", default_font());
    const std::string mode   = string_arg(args, "mode", "new_object");
    if (mode != "new_object" && mode != "emboss_top" && mode != "engrave_top")
        throw BridgeError(400, "invalid_args", "mode must be new_object, emboss_top or engrave_top");
    const int object = optional_index_arg(args, "object", model.objects.size());
    if (mode != "new_object" && (object < 0 || model.objects[object]->instances.empty()))
        throw BridgeError(400, "invalid_args", mode + " needs object: the object whose top gets the text");
    // The font parser does little checking, so only real font files the user named are read.
    {
        const std::string ext = boost::algorithm::to_lower_copy(fs::path(font).extension().string());
        boost::system::error_code ec;
        if (!fs::path(font).is_absolute() || (ext != ".ttf" && ext != ".otf" && ext != ".ttc"))
            throw BridgeError(400, "invalid_args", "font_path must be an absolute path to a .ttf, .otf or .ttc file");
        if (!fs::is_regular_file(font, ec))
            throw BridgeError(404, "not_found", "No font file at " + font);
        if (fs::file_size(font, ec) > 64u * 1024 * 1024)
            throw BridgeError(400, "invalid_args", "The font file is larger than 64 MB");
    }

    // The mesh is built before the change starts, so a bad font or text changes nothing.
    TriangleMesh mesh = text_mesh(text, font, size, mode == "engrave_top" ? depth + 0.2 : depth);
    json         target = {{"text", text}, {"mode", mode}};
    if (object >= 0)
        target["object"] = object + 1;

    return run_geometry_change("add_text", target, [&](json &result) {
        if (mode == "new_object") {
            // Loading a file gives the new object Studio's usual placement on the current plate.
            const fs::path file = fs::temp_directory_path() / (file_safe(text) + ".stl");
            if (!store_stl(file.string().c_str(), &mesh, true))
                throw BridgeError(500, "write_failed", "Could not write the text mesh to " + file.string());
            const std::vector<size_t> added = plater.load_files(std::vector<std::string>{file.string()}, LoadStrategy::LoadModel, false);
            boost::system::error_code ec;
            fs::remove(file, ec);
            if (added.empty())
                throw BridgeError(422, "load_failed", "Studio did not add the text object.");
            result["added_objects"] = json::array({added.front() + 1});
            return;
        }
        ModelObject        &target_object = *model.objects[object];
        const ModelInstance &instance     = *target_object.instances.front();
        const BoundingBoxf3  box          = target_object.instance_bounding_box(0);
        // Raised text sits on the top; engraved text reaches depth below it and pokes 0.2 mm above.
        const double         base_z       = mode == "emboss_top" ? box.max.z() : box.max.z() - depth;
        const Vec3d          local        = instance.get_matrix().inverse() * Vec3d(box.center().x(), box.center().y(), base_z);
        const Transform3d    volume_tr    = Geometry::translation_transform(local) * Transform3d(instance.get_matrix(true).inverse());
        ModelVolume *volume = target_object.add_volume(std::move(mesh), mode == "emboss_top" ? ModelVolumeType::MODEL_PART : ModelVolumeType::NEGATIVE_VOLUME, false);
        volume->name = "Text " + text;
        volume->set_transformation(Geometry::Transformation(volume_tr));
        target_object.invalidate_bounding_box();
        if (ObjectList *list = wxGetApp().obj_list())
            list->add_volumes_to_object_in_list(size_t(object));
        plater.changed_object(object);
        result["part"] = target_object.volumes.size();
    });
}

}}} // namespace Slic3r::GUI::AgentBridge
