#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "khdays/assets/mesh.h"

namespace {

void write_u16(
    std::vector<std::uint8_t>& data,
    const std::size_t offset,
    const std::uint16_t value) {
    data.at(offset) = static_cast<std::uint8_t>(value & 0xFFU);
    data.at(offset + 1U) = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
}

void write_u32(
    std::vector<std::uint8_t>& data,
    const std::size_t offset,
    const std::uint32_t value) {
    data.at(offset) = static_cast<std::uint8_t>(value & 0xFFU);
    data.at(offset + 1U) = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
    data.at(offset + 2U) = static_cast<std::uint8_t>((value >> 16U) & 0xFFU);
    data.at(offset + 3U) = static_cast<std::uint8_t>((value >> 24U) & 0xFFU);
}

void write_magic(
    std::vector<std::uint8_t>& data,
    const std::size_t offset,
    const std::string& magic) {
    for (std::size_t index = 0U; index < magic.size(); ++index) {
        data.at(offset + index) = static_cast<std::uint8_t>(magic[index]);
    }
}

// Build a Nitro name dictionary with a single entry, matching the layout the
// decoder expects: a 4-byte relative offset followed by a 16-byte name.
std::vector<std::uint8_t> make_dictionary(
    const std::string& name,
    const std::uint32_t relative_offset) {
    constexpr std::size_t dictionary_size = 40U;
    std::vector<std::uint8_t> data(dictionary_size, 0U);

    data[1] = 1U;                            // entry count
    write_u16(data, 2U, dictionary_size);
    write_u16(data, 4U, 8U);
    write_u16(data, 6U, 16U);                // entry header offset
    write_u16(data, 16U, 4U);                // entry unit size
    write_u16(data, 18U, 8U);                // names offset
    write_u32(data, 20U, relative_offset);   // the single entry

    for (std::size_t index = 0U; index < name.size() && index < 15U; ++index) {
        data[24U + index] = static_cast<std::uint8_t>(name[index]);
    }

    return data;
}

// A BMD0 with one bone-less model drawing one triangle (three VTX_16 at the unit axes), so the palette
// is the identity; `scaled`: its display list first restores matrix 0 and scales it by 2, as models
// with a position scale do (the Darkside, DS01).
std::vector<std::uint8_t> wrap_bmd0(const std::vector<std::uint8_t>& mdl0, std::size_t mdl0_offset_in_file);

std::vector<std::uint8_t> make_triangle_bmd0(const bool scaled = false) {
    constexpr std::size_t mdl0_offset_in_file = 0x14U;
    constexpr std::size_t model_offset = 0x30U;    // within the MDL0 section
    constexpr std::size_t render_off = 0x80U;      // relative to the model
    constexpr std::size_t materials_off = 0x90U;
    constexpr std::size_t pieces_off = 0xB0U;
    constexpr std::size_t mesh_relative = 0x30U;   // relative to the mesh list
    constexpr std::size_t mesh_list_offset = model_offset + pieces_off;
    constexpr std::size_t mesh_offset = mesh_list_offset + mesh_relative;
    constexpr std::size_t command_relative = 0x10U;
    constexpr std::size_t command_offset = mesh_offset + command_relative;
    const std::size_t command_length = (scaled ? 13U : 9U) * 4U;  // packed words
    const std::size_t mdl0_size = command_offset + command_length;

    std::vector<std::uint8_t> mdl0(mdl0_size, 0U);
    write_magic(mdl0, 0U, "MDL0");
    write_u32(mdl0, 4U, static_cast<std::uint32_t>(mdl0_size));

    const auto model_dictionary =
        make_dictionary("tri_model", static_cast<std::uint32_t>(model_offset));
    std::copy(model_dictionary.begin(), model_dictionary.end(), mdl0.begin() + 8);

    write_u32(mdl0, model_offset + 0x04U, static_cast<std::uint32_t>(render_off));
    write_u32(mdl0, model_offset + 0x08U, static_cast<std::uint32_t>(materials_off));
    write_u32(mdl0, model_offset + 0x0CU, static_cast<std::uint32_t>(pieces_off));
    write_u32(mdl0, model_offset + 0x10U, 0U);       // inverse binds (unused)
    mdl0[model_offset + 0x17U] = 0U;                 // num objects (bones)
    write_u32(mdl0, model_offset + 0x1CU, 0x1000U);  // up scale = 1.0 (20.12)
    write_u16(mdl0, model_offset + 0x24U, 3U);       // header vertex count

    // Render commands: Draw piece 0, then End.
    mdl0[model_offset + render_off + 0U] = 0x05U;
    mdl0[model_offset + render_off + 1U] = 0x00U;
    mdl0[model_offset + render_off + 2U] = 0x01U;

    const auto mesh_dictionary =
        make_dictionary("tri_mesh", static_cast<std::uint32_t>(mesh_relative));
    std::copy(
        mesh_dictionary.begin(),
        mesh_dictionary.end(),
        mdl0.begin() + static_cast<std::ptrdiff_t>(mesh_list_offset));

    write_u16(mdl0, mesh_offset + 2U, 16U);  // header size
    write_u32(mdl0, mesh_offset + 8U, static_cast<std::uint32_t>(command_relative));
    write_u32(mdl0, mesh_offset + 12U, static_cast<std::uint32_t>(command_length));

    // Command stream: BEGIN_VTXS(triangles), 3x VTX_16, END_VTXS (after
    // MTX_RESTORE 0 and MTX_SCALE 2,2,2 when scaled).
    std::size_t cursor = command_offset;
    if (scaled) {
        write_u32(mdl0, cursor, 0x23401B14U);  // [MTX_RESTORE, MTX_SCALE, BEGIN_VTXS, VTX_16]
        cursor += 4U;
        write_u32(mdl0, cursor, 0x00000000U);  // MTX_RESTORE: matrix 0
        cursor += 4U;
        for (int axis = 0; axis < 3; ++axis) {
            write_u32(mdl0, cursor, 0x00002000U);  // MTX_SCALE: 2.0 (1.19.12)
            cursor += 4U;
        }
        write_u32(mdl0, cursor, 0x00000000U);  // BEGIN_VTXS param: mode 0
        cursor += 4U;
        write_u32(mdl0, cursor, 0x00001000U);  // v0 p0: X=4096, Y=0
        cursor += 4U;
        write_u32(mdl0, cursor, 0x00000000U);  // v0 p1: Z=0
        cursor += 4U;
        write_u32(mdl0, cursor, 0x00412323U);  // [VTX_16, VTX_16, END_VTXS, NOP]
        cursor += 4U;
        write_u32(mdl0, cursor, 0x10000000U);  // v1 p0: X=0, Y=4096
        cursor += 4U;
        write_u32(mdl0, cursor, 0x00000000U);  // v1 p1: Z=0
        cursor += 4U;
        write_u32(mdl0, cursor, 0x00000000U);  // v2 p0: X=0, Y=0
        cursor += 4U;
        write_u32(mdl0, cursor, 0x00001000U);  // v2 p1: Z=4096
        return wrap_bmd0(mdl0, mdl0_offset_in_file);
    }
    write_u32(mdl0, cursor, 0x23232340U);  // [BEGIN_VTXS, VTX_16, VTX_16, VTX_16]
    cursor += 4U;
    write_u32(mdl0, cursor, 0x00000000U);  // BEGIN_VTXS param: mode 0
    cursor += 4U;
    write_u32(mdl0, cursor, 0x00001000U);  // v0 p0: X=4096, Y=0
    cursor += 4U;
    write_u32(mdl0, cursor, 0x00000000U);  // v0 p1: Z=0
    cursor += 4U;
    write_u32(mdl0, cursor, 0x10000000U);  // v1 p0: X=0, Y=4096
    cursor += 4U;
    write_u32(mdl0, cursor, 0x00000000U);  // v1 p1: Z=0
    cursor += 4U;
    write_u32(mdl0, cursor, 0x00000000U);  // v2 p0: X=0, Y=0
    cursor += 4U;
    write_u32(mdl0, cursor, 0x00001000U);  // v2 p1: Z=4096
    cursor += 4U;
    write_u32(mdl0, cursor, 0x00000041U);  // [END_VTXS, NOP, NOP, NOP]
    return wrap_bmd0(mdl0, mdl0_offset_in_file);
}

// Wrap an MDL0 in a minimal single-section BMD0 container.
std::vector<std::uint8_t> wrap_bmd0(const std::vector<std::uint8_t>& mdl0, const std::size_t mdl0_offset_in_file) {
    const auto file_size = mdl0_offset_in_file + mdl0.size();
    std::vector<std::uint8_t> file(file_size, 0U);
    write_magic(file, 0U, "BMD0");
    write_u16(file, 4U, 0xFEFFU);
    write_u16(file, 6U, 2U);
    write_u32(file, 8U, static_cast<std::uint32_t>(file_size));
    write_u16(file, 0x0CU, 0x10U);
    write_u16(file, 0x0EU, 1U);  // one section
    write_u32(file, 0x10U, static_cast<std::uint32_t>(mdl0_offset_in_file));

    std::copy(
        mdl0.begin(),
        mdl0.end(),
        file.begin() + static_cast<std::ptrdiff_t>(mdl0_offset_in_file));

    return file;
}

bool close_to(const float value, const float expected) {
    return std::fabs(value - expected) < 1.0e-4F;
}

}  // namespace

int main() {
    const auto path =
        std::filesystem::temp_directory_path() / "khdays_mesh_test.nsbmd";

    try {
        const auto data = make_triangle_bmd0();
        {
            std::ofstream stream{path, std::ios::binary};
            stream.write(
                reinterpret_cast<const char*>(data.data()),
                static_cast<std::streamsize>(data.size()));
        }

        const auto model = khdays::assets::decode_model_geometry(path);

        if (model.name != "tri_model") {
            throw std::runtime_error("unexpected model name");
        }
        if (model.header_vertex_count != 3U) {
            throw std::runtime_error("unexpected header vertex count");
        }
        if (model.meshes.size() != 1U
            || model.meshes.front().name != "tri_mesh") {
            throw std::runtime_error("unexpected mesh list");
        }

        const auto& mesh = model.meshes.front();

        // The decoded vertex count must equal the header vertex count.
        if (mesh.vertices.size() != model.header_vertex_count) {
            throw std::runtime_error(
                "decoded vertex count does not match the header");
        }
        if (mesh.indices.size() != 3U) {
            throw std::runtime_error("expected exactly one triangle");
        }
        if (mesh.indices[0] != 0U
            || mesh.indices[1] != 1U
            || mesh.indices[2] != 2U) {
            throw std::runtime_error("unexpected triangle winding");
        }

        const auto& v = mesh.vertices;
        if (!close_to(v[0].position[0], 1.0F)
            || !close_to(v[0].position[1], 0.0F)
            || !close_to(v[0].position[2], 0.0F)
            || !close_to(v[1].position[1], 1.0F)
            || !close_to(v[2].position[2], 1.0F)) {
            throw std::runtime_error("decoded positions are incorrect");
        }

        // A piece's MTX_SCALE scales the restored matrix (the DS's geometry
        // engine does; the Darkside's arms were half their size without it),
        // and the program replayed for animation gives the same matrix.
        {
            const auto scaled_data = make_triangle_bmd0(true);
            const auto scaled = khdays::assets::decode_model_geometry(scaled_data.data(), scaled_data.size(), 0U);
            if (scaled.meshes.size() != 1U || scaled.meshes.front().vertices.size() != 3U) {
                throw std::runtime_error("scaled triangle not decoded");
            }
            const auto& sv = scaled.meshes.front().vertices;
            const auto posed = [&](const khdays::assets::NeutralVertex& v) {
                // the weighted blend of the palette's matrices applied to the raw position
                std::array<float, 3> out{};
                for (std::size_t j = 0U; j < v.joints.size(); ++j) {
                    if (v.weights[j] == 0.0F) {
                        continue;
                    }
                    const auto& m = scaled.palette[v.joints[j]];
                    for (std::size_t r = 0U; r < 3U; ++r) {
                        out[r] += v.weights[j] * (m[r] * v.position[0] + m[4U + r] * v.position[1]
                                                  + m[8U + r] * v.position[2] + m[12U + r]);
                    }
                }
                return out;
            };
            const auto p0 = posed(sv[0]);
            const auto p2 = posed(sv[2]);
            if (!close_to(p0[0], 2.0F) || !close_to(p0[1], 0.0F) || !close_to(p2[2], 2.0F)) {
                throw std::runtime_error("MTX_SCALE not applied to the piece's vertices");
            }
            const auto replayed = khdays::assets::compute_palette(*scaled.skinning, scaled.object_matrices);
            if (replayed.size() != scaled.palette.size()) {
                throw std::runtime_error("replayed palette differs in size");
            }
            for (std::size_t e = 0U; e < replayed.size(); ++e) {
                for (std::size_t k = 0U; k < 16U; ++k) {
                    if (!close_to(replayed[e][k], scaled.palette[e][k])) {
                        throw std::runtime_error("replayed palette differs");
                    }
                }
            }
        }

        std::filesystem::remove(path);
        std::cout << "Mesh decoder test passed" << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::filesystem::remove(path);
        std::cerr << "Mesh decoder test failed: " << error.what() << '\n';
        return 1;
    }
}
