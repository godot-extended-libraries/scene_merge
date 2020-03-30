/*************************************************************************/
/*  mesh.cpp                                                             */
/*************************************************************************/
/*                       This file is part of:                           */
/*                           GODOT ENGINE                                */
/*                      https://godotengine.org                          */
/*************************************************************************/
/* Copyright (c) 2007-2019 Juan Linietsky, Ariel Manzur.                 */
/* Copyright (c) 2014-2019 Godot Engine contributors (cf. AUTHORS.md)    */
/*                                                                       */
/* Permission is hereby granted, free of charge, to any person obtaining */
/* a copy of this software and associated documentation files (the       */
/* "Software"), to deal in the Software without restriction, including   */
/* without limitation the rights to use, copy, modify, merge, publish,   */
/* distribute, sublicense, and/or sell copies of the Software, and to    */
/* permit persons to whom the Software is furnished to do so, subject to */
/* the following conditions:                                             */
/*                                                                       */
/* The above copyright notice and this permission notice shall be        */
/* included in all copies or substantial portions of the Software.       */
/*                                                                       */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,       */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF    */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.*/
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY  */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,  */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE     */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                */
/*************************************************************************/

/*
xatlas
https://github.com/jpcy/xatlas
Copyright (c) 2018 Jonathan Young
Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/
/*
thekla_atlas
https://github.com/Thekla/thekla_atlas
MIT License
Copyright (c) 2013 Thekla, Inc
Copyright NVIDIA Corporation 2006 -- Ignacio Castano <icastano@nvidia.com>
*/

#include <stdio.h>
#include <stdlib.h>
#define TEXBLEED_IMPLEMENTATION 1
#include "thirdparty/misc/rjm_texbleed.h"

#include "core/math/vector2.h"
#include "core/math/vector3.h"
#include "core/os/os.h"
#include "scene/resources/mesh_data_tool.h"
#include "scene/resources/packed_scene.h"
#include "scene/resources/surface_tool.h"

#include "merge.h"

void SceneMerge::_node_replace_owner(Node *p_base, Node *p_node, Node *p_root) {

	p_node->set_owner(p_root);
	p_node->set_filename("");

	for (int child_i = 0; child_i < p_node->get_child_count(); child_i++) {
		_node_replace_owner(p_base, p_node->get_child(child_i), p_root);
	}
}

void SceneMerge::merge(const String p_file, Node *p_root_node) {
	PackedScene *scene = memnew(PackedScene);
	scene->pack(p_root_node);
	Node *root = scene->instance();
	_node_replace_owner(root, root, root);
	Ref<MeshMergeMaterialRepack> repack;
	repack.instance();
	root = repack->merge(root, p_root_node);
	scene->pack(root);
	ResourceSaver::save(p_file, scene);
}

bool MeshMergeMaterialRepack::setAtlasTexel(void *param, int x, int y, const Vector3 &bar, const Vector3 &, const Vector3 &, float) {
	SetAtlasTexelArgs *args = (SetAtlasTexelArgs *)param;
	if (args->sourceTexture.is_valid()) {
		// Interpolate source UVs using barycentrics.
		const Vector2 sourceUv = args->source_uvs[0] * bar.x + args->source_uvs[1] * bar.y + args->source_uvs[2] * bar.z;
		// Keep coordinates in range of texture dimensions.
		int _width = args->sourceTexture->get_width();
		float sx = sourceUv.x * _width;
		while (sx < 0) {
			sx += _width;
		}
		if ((int32_t)sx >= _width) {
			sx = Math::fmod(sx, _width);
		}
		int _height = args->sourceTexture->get_height();
		float sy = sourceUv.y * _height;
		while (sy < 0) {
			sy += _height;
		}
		if ((int32_t)sy >= _height) {
			sy = Math::fmod(sy, _height);
		}
		args->sourceTexture->lock();
		const Color color = args->sourceTexture->get_pixel(sx, sy);
		args->sourceTexture->unlock();
		args->atlasData->lock();
		args->atlasData->set_pixel(x, y, color);
		args->atlasData->unlock();

		AtlasLookupTexel &lookup = args->atlas_lookup.write[x * y + args->atlas_width];
		lookup.material_index = args->material_index;
		lookup.x = (uint16_t)sx;
		lookup.y = (uint16_t)sy;
		return true;
	}
	return false;
}

void MeshMergeMaterialRepack::_find_all_mesh_instances(Vector<MeshInstance *> &r_items, Node *p_current_node, const Node *p_owner) {
	MeshInstance *mi = Object::cast_to<MeshInstance>(p_current_node);
	if (mi) {
		Ref<ArrayMesh> array_mesh = mi->get_mesh();
		if (!array_mesh.is_null()) {
			bool has_blends = false;
			bool has_bones = false;
			bool has_emission = false;
			for (int32_t surface_i = 0; surface_i < array_mesh->get_surface_count(); surface_i++) {
				Array array = array_mesh->surface_get_arrays(surface_i);
				Array bones = array[ArrayMesh::ARRAY_BONES];
				if (bones.size()) {
					has_bones |= true;
				}
				if (array_mesh->get_blend_shape_count()) {
					has_blends |= true;
				}
				Ref<SpatialMaterial> spatial_mat = array_mesh->surface_get_material(surface_i);
				if (spatial_mat.is_valid()) {
					Ref<Image> img = spatial_mat->get_texture(SpatialMaterial::TEXTURE_ALBEDO);
					if (spatial_mat->get_feature(SpatialMaterial::FEATURE_EMISSION)) {
						has_emission |= true;
					}
				}
			}
			if (!has_blends && !has_bones && !has_emission) {
				r_items.push_back(mi);
			}
		}
	}
	for (int32_t child_i = 0; child_i < p_current_node->get_child_count(); child_i++) {
		_find_all_mesh_instances(r_items, p_current_node->get_child(child_i), p_owner);
	}
}

Node *MeshMergeMaterialRepack::merge(Node *p_root, Node *p_original_root) {
	Vector<MeshInstance *> mesh_items;
	_find_all_mesh_instances(mesh_items, p_root, p_root);
	Vector<MeshInstance *> original_mesh_items;
	_find_all_mesh_instances(original_mesh_items, p_original_root, p_original_root);
	if (!original_mesh_items.size()) {
		return p_root;
	}
	Array vertex_to_material;
	Vector<Ref<Material> > material_cache;
	Ref<Material> empty_material;
	material_cache.push_back(empty_material);
	map_vertex_to_material(mesh_items, vertex_to_material, material_cache);

	Vector<Vector<Vector2> > uv_groups;
	Vector<Vector<ModelVertex> > model_vertices;
	scale_uvs_by_texture_dimension(original_mesh_items, mesh_items, uv_groups, vertex_to_material, model_vertices);
	xatlas::SetPrint(printf, true);
	xatlas::Atlas *atlas = xatlas::Create();

	int32_t num_surfaces = 0;

	for (int32_t mesh_i = 0; mesh_i < mesh_items.size(); mesh_i++) {
		for (int32_t j = 0; j < mesh_items[mesh_i]->get_mesh()->get_surface_count(); j++) {
			Array mesh = mesh_items[mesh_i]->get_mesh()->surface_get_arrays(j);
			if (mesh.empty()) {
				continue;
			}
			Vector<Vector3> vertices = mesh[ArrayMesh::ARRAY_VERTEX];
			if (!vertices.size()) {
				continue;
			}
			num_surfaces++;
		}
	}
	xatlas::PackOptions pack_options;
	Vector<AtlasLookupTexel> atlas_lookup;
	_generate_atlas(num_surfaces, uv_groups, atlas, mesh_items, vertex_to_material, material_cache, pack_options);
	atlas_lookup.resize(atlas->width * atlas->height);
	Map<String, Ref<Image> > texture_atlas;
	MergeState state = { p_root, atlas, mesh_items, vertex_to_material, uv_groups, model_vertices, p_root->get_name(), pack_options, atlas_lookup, material_cache, texture_atlas, false};

	print_line("Generating albedo texture atlas.");
	_generate_texture_atlas(state, "albedo");
	print_line("Generating emission texture atlas.");
	_generate_texture_atlas(state, "emission");
	print_line("Generating normal texture atlas.");
	_generate_texture_atlas(state, "normal");
	print_line("Generating orm texture atlas.");
	_generate_texture_atlas(state, "orm");
	ERR_FAIL_COND_V(state.atlas->width <= 0 && state.atlas->height <= 0, state.p_root);
	p_root = _output(state);

	xatlas::Destroy(atlas);
	return p_root;
}

void MeshMergeMaterialRepack::_generate_texture_atlas(MergeState &state, String texture_type) {
	Ref<Image> atlas_img;
	atlas_img.instance();
	atlas_img->create(state.atlas->width, state.atlas->height, false, Image::FORMAT_RGBA8);
	// Rasterize chart triangles.
	Map<uint16_t, Ref<Image> > image_cache;
	for (uint32_t mesh_i = 0; mesh_i < state.atlas->meshCount; mesh_i++) {
		const xatlas::Mesh &mesh = state.atlas->meshes[mesh_i];
		print_line("  mesh atlas " + itos(mesh_i));
		for (uint32_t j = 0; j < mesh.chartCount; j++) {
			const xatlas::Chart &chart = mesh.chartArray[j];
			Ref<SpatialMaterial> material;
			Ref<Image> img = _get_source_texture(state, image_cache, chart, material, texture_type);
			ERR_CONTINUE_MSG(Image::get_format_pixel_size(img->get_format()) > 4, "Float textures are not supported yet");
			Ref<ImageTexture> image_texture;
			image_texture.instance();
			image_texture->create_from_image(img);
			img->convert(Image::FORMAT_RGBA8);
			SetAtlasTexelArgs args;
			args.sourceTexture = img;
			args.atlasData = atlas_img;
			args.atlas_lookup = state.atlas_lookup;
			args.material_index = (uint16_t)chart.material;
			for (uint32_t face_i = 0; face_i < chart.faceCount; face_i++) {
				Vector2 v[3];
				for (uint32_t l = 0; l < 3; l++) {
					const uint32_t index = mesh.indexArray[chart.faceArray[face_i] * 3 + l];
					const xatlas::Vertex &vertex = mesh.vertexArray[index];
					v[l] = Vector2(vertex.uv[0], vertex.uv[1]);
					args.source_uvs[l].x = state.uvs[mesh_i][vertex.xref].x / img->get_width();
					args.source_uvs[l].y = state.uvs[mesh_i][vertex.xref].y / img->get_height();
				}
				Triangle tri(v[0], v[1], v[2], Vector3(1, 0, 0), Vector3(0, 1, 0), Vector3(0, 0, 1));

				tri.drawAA(setAtlasTexel, &args);
			}
		}
	}
	state.texture_atlas.insert(texture_type, atlas_img);
}

Ref<Image> MeshMergeMaterialRepack::_get_source_texture(MergeState &state, Map<uint16_t, Ref<Image> > &image_cache, const xatlas::Chart &chart, Ref<SpatialMaterial> &material, String texture_type) {
	Ref<Image> img;
	float width = 1;
	float height = 1;
	Ref<Image> ao_img;
	material = state.material_cache.get(chart.material);
	ERR_FAIL_COND_V(material.is_null(), nullptr);
	ao_img = material->get_texture(SpatialMaterial::TEXTURE_AMBIENT_OCCLUSION);
	Ref<Image> metallic_img = material->get_texture(SpatialMaterial::TEXTURE_METALLIC);
	Ref<Image> roughness_img = material->get_texture(SpatialMaterial::TEXTURE_ROUGHNESS);
	Ref<Image> albedo_img = material->get_texture(SpatialMaterial::TEXTURE_ALBEDO);
	Ref<Image> emission_img = material->get_texture(SpatialMaterial::TEXTURE_EMISSION);
	Ref<Image> normal_img = material->get_texture(SpatialMaterial::TEXTURE_NORMAL);
	if (ao_img.is_valid() && !ao_img->empty()) {
		width = MAX(width, ao_img->get_width());
		height = MAX(height, ao_img->get_height());
	}
	if (metallic_img.is_valid() && !metallic_img->empty()) {
		width = MAX(width, metallic_img->get_width());
		height = MAX(height, metallic_img->get_height());
	}
	if (roughness_img.is_valid() && !roughness_img->empty()) {
		width = MAX(width, roughness_img->get_width());
		height = MAX(height, roughness_img->get_height());
	}
	if (albedo_img.is_valid() && !albedo_img->empty()) {
		width = MAX(width, albedo_img->get_width());
		height = MAX(height, albedo_img->get_height());
	}
	if (emission_img.is_valid() && !emission_img->empty()) {
		width = MAX(width, emission_img->get_width());
		height = MAX(height, emission_img->get_height());
	}
	if (normal_img.is_valid() && !normal_img->empty()) {
		width = MAX(width, normal_img->get_width());
		height = MAX(height, normal_img->get_height());
	}
	img.instance();
	img->create(width, height, false, Image::FORMAT_RGBA8);
	Map<uint16_t, Ref<Image> >::Element *E = image_cache.find(chart.material);
	if (E) {
		img = E->get();
	} else {
		Ref<Texture> tex;
		if (texture_type == "orm") {
			for (int32_t y = 0; y < img->get_height(); y++) {
				for (int32_t x = 0; x < img->get_width(); x++) {
					Color orm;
					if (ao_img.is_valid() && !ao_img->empty()) {
						ao_img->resize(width, height, Image::INTERPOLATE_LANCZOS);
						ao_img->lock();
						if (material->get_ao_texture_channel() == SpatialMaterial::TEXTURE_CHANNEL_RED) {
							orm.r = ao_img->get_pixel(x, y).r;
						} else if (material->get_ao_texture_channel() == SpatialMaterial::TEXTURE_CHANNEL_GREEN) {
							orm.r = ao_img->get_pixel(x, y).g;
						} else if (material->get_ao_texture_channel() == SpatialMaterial::TEXTURE_CHANNEL_BLUE) {
							orm.r = ao_img->get_pixel(x, y).b;
						} else if (material->get_ao_texture_channel() == SpatialMaterial::TEXTURE_CHANNEL_ALPHA) {
							orm.r = ao_img->get_pixel(x, y).a;
						} else if (material->get_ao_texture_channel() == SpatialMaterial::TEXTURE_CHANNEL_GRAYSCALE) {
							orm.r = ao_img->get_pixel(x, y).r;
						}
						ao_img->unlock();
					}
					float channel_mul;
					float channel_add;
					if (roughness_img.is_valid()) {
						channel_mul = material->get_roughness();
						channel_add = 0.0f;
					} else {
						channel_mul = 1.0f;
						channel_add = material->get_roughness();
					}
					if (roughness_img.is_valid() && !roughness_img->empty()) {
						roughness_img->resize(width, height, Image::INTERPOLATE_LANCZOS);
						roughness_img->lock();
						if (material->get_roughness_texture_channel() == SpatialMaterial::TEXTURE_CHANNEL_RED) {
							orm.g = roughness_img->get_pixel(x, y).r;
						} else if (material->get_roughness_texture_channel() == SpatialMaterial::TEXTURE_CHANNEL_GREEN) {
							orm.g = roughness_img->get_pixel(x, y).g;
						} else if (material->get_roughness_texture_channel() == SpatialMaterial::TEXTURE_CHANNEL_BLUE) {
							orm.g = roughness_img->get_pixel(x, y).b;
						} else if (material->get_roughness_texture_channel() == SpatialMaterial::TEXTURE_CHANNEL_ALPHA) {
							orm.g = roughness_img->get_pixel(x, y).a;
						} else if (material->get_roughness_texture_channel() == SpatialMaterial::TEXTURE_CHANNEL_GRAYSCALE) {
							orm.g = roughness_img->get_pixel(x, y).r;
						}
						roughness_img->unlock();
					}
					orm.g = orm.g * channel_mul + channel_add;
					if (metallic_img.is_valid()) {
						channel_mul = material->get_metallic();
						channel_add = 0.0f;
					} else {
						channel_mul = 1.0f;
						channel_add = material->get_metallic();
					}
					if (metallic_img.is_valid() && !metallic_img->empty()) {
						metallic_img->resize(width, height, Image::INTERPOLATE_LANCZOS);
						metallic_img->lock();

						if (material->get_metallic_texture_channel() == SpatialMaterial::TEXTURE_CHANNEL_RED) {
							orm.b = metallic_img->get_pixel(x, y).r;
						} else if (material->get_metallic_texture_channel() == SpatialMaterial::TEXTURE_CHANNEL_GREEN) {
							orm.b = metallic_img->get_pixel(x, y).g;
						} else if (material->get_metallic_texture_channel() == SpatialMaterial::TEXTURE_CHANNEL_BLUE) {
							orm.b = metallic_img->get_pixel(x, y).b;
						} else if (material->get_metallic_texture_channel() == SpatialMaterial::TEXTURE_CHANNEL_ALPHA) {
							orm.b = metallic_img->get_pixel(x, y).a;
						} else if (material->get_metallic_texture_channel() == SpatialMaterial::TEXTURE_CHANNEL_GRAYSCALE) {
							orm.b = metallic_img->get_pixel(x, y).r;
						}
						metallic_img->unlock();
					}
					orm.b = orm.b * channel_mul + channel_add;
					img->lock();
					img->set_pixel(x, y, orm);
					img->unlock();
				}
			}
		} else if (texture_type == "albedo") {
			tex = material->get_texture(SpatialMaterial::TEXTURE_ALBEDO);
			if (material->get_feature(SpatialMaterial::FEATURE_TRANSPARENT)) {
				state.is_transparent = true;
			}
			Color color_mul;
			Color color_add;
			if (tex.is_valid()) {
				color_mul = material->get_albedo();
				color_add = Color(0, 0, 0, 0);
			} else {
				color_mul = Color(1, 1, 1, 1);
				color_add = material->get_albedo();
			}
			if (tex.is_valid()) {
				img = tex->get_data();
				if (!img->empty()) {
					if (img->is_compressed()) {
						img->decompress();
					}
				}
			}
			img->lock();
			for (int32_t y = 0; y < img->get_height(); y++) {
				for (int32_t x = 0; x < img->get_width(); x++) {
					Color c = img->get_pixel(x, y);
					c.r = c.r * color_mul.r + color_add.r;
					c.g = c.g * color_mul.g + color_add.g;
					c.b = c.b * color_mul.b + color_add.b;
					img->set_pixel(x, y, c);
				}
			}
			img->unlock();
		} else if (texture_type == "emission") {
			if (!material->get_feature(SpatialMaterial::FEATURE_EMISSION)) {
				return img;
			}
			tex = material->get_texture(SpatialMaterial::TEXTURE_EMISSION);
			Color emission_col = material->get_emission();
			float emission_energy = material->get_emission_energy();
			Color color_mul;
			Color color_add;
			if (material->get_emission_operator() == SpatialMaterial::EMISSION_OP_ADD) {
				color_mul = Color(1, 1, 1) * emission_energy;
				color_add = emission_col * emission_energy;
			} else {
				color_mul = emission_col * emission_energy;
				color_add = Color(0, 0, 0);
			}
			if (tex.is_valid()) {
				img = tex->get_data();
				if (!img->empty()) {
					if (img->is_compressed()) {
						img->decompress();
					}
				}
			}
			img->lock();
			for (int32_t y = 0; y < img->get_height(); y++) {
				for (int32_t x = 0; x < img->get_width(); x++) {
					Color c = img->get_pixel(x, y);
					c.r = c.r * color_mul.r + color_add.r;
					c.g = c.g * color_mul.g + color_add.g;
					c.b = c.b * color_mul.b + color_add.b;
					img->set_pixel(x, y, c);
				}
			}
			img->unlock();
		} else if (texture_type == "normal") {
			if (!material->get_feature(SpatialMaterial::FEATURE_NORMAL_MAPPING)) {
				return img;
			}
			tex = material->get_texture(SpatialMaterial::TEXTURE_NORMAL);
			if (tex.is_valid()) {
				img = tex->get_data();
				if (!img->empty()) {
					if (img->is_compressed()) {
						img->decompress();
					}
				}
			}
		}
	}
	image_cache.insert(chart.material, img);
	return img;
}

void MeshMergeMaterialRepack::_generate_atlas(const int32_t p_num_meshes, Vector<Vector<Vector2> > &r_uvs, xatlas::Atlas *atlas, const Vector<MeshInstance *> &r_meshes, Array vertex_to_material, const Vector<Ref<Material> > material_cache,
		xatlas::PackOptions &pack_options) {

	int32_t mesh_first_index = 0;
	uint32_t mesh_count = 0;
	for (int32_t mesh_i = 0; mesh_i < r_meshes.size(); mesh_i++) {
		for (int32_t j = 0; j < r_meshes[mesh_i]->get_mesh()->get_surface_count(); j++) {
			// Handle blend shapes?
			Array mesh = r_meshes[mesh_i]->get_mesh()->surface_get_arrays(j);
			if (mesh.empty()) {
				continue;
			}
			Array vertices = mesh[ArrayMesh::ARRAY_VERTEX];
			if (vertices.empty()) {
				continue;
			}
			Array indices = mesh[ArrayMesh::ARRAY_INDEX];
			if (indices.empty()) {
				continue;
			}
			xatlas::UvMeshDecl meshDecl;
			meshDecl.vertexCount = r_uvs[mesh_count].size();
			meshDecl.vertexUvData = r_uvs[mesh_count].ptr();
			meshDecl.vertexStride = sizeof(Vector2);
			Vector<int32_t> mesh_indices = mesh[Mesh::ARRAY_INDEX];
			Vector<uint32_t> indexes;
			indexes.resize(mesh_indices.size());
			Vector<uint32_t> materials;
			materials.resize(mesh_indices.size());
			const Array materials_array = vertex_to_material[mesh_count];
			for (int32_t index_i = 0; index_i < mesh_indices.size(); index_i++) {
				indexes.write[index_i] = mesh_indices[index_i];
				ERR_FAIL_COND(index_i >= materials_array.size());
				Ref<Material> material = materials_array[index_i];
				if (!material.is_valid()) {
					continue;
				}
				if (material_cache.find(material) == -1) {
					continue;
				}
				materials.write[index_i] = material_cache.find(material);
			}
			meshDecl.indexCount = indexes.size();
			meshDecl.indexData = indexes.ptr();
			meshDecl.indexFormat = xatlas::IndexFormat::UInt32;
			meshDecl.indexOffset = 0;
			meshDecl.faceMaterialData = materials.ptr();
			meshDecl.rotateCharts = false;
			xatlas::AddMeshError::Enum error = xatlas::AddUvMesh(atlas, meshDecl);
			if (error != xatlas::AddMeshError::Success) {
				OS::get_singleton()->print("Error adding mesh %d: %s\n", mesh_i, xatlas::StringForEnum(error));
				ERR_CONTINUE(error != xatlas::AddMeshError::Success);
			}
			mesh_first_index += vertices.size();
			mesh_count++;
		}
	}
	pack_options.texelsPerUnit = 1.0f;
	pack_options.maxChartSize = 4096;
	pack_options.blockAlign = true;
	xatlas::PackCharts(atlas, pack_options);
}

void MeshMergeMaterialRepack::scale_uvs_by_texture_dimension(const Vector<MeshInstance *> &original_mesh_items, Vector<MeshInstance *> &mesh_items, Vector<Vector<Vector2> > &uv_groups, Array &r_vertex_to_material, Vector<Vector<ModelVertex> > &r_model_vertices) {
	for (int32_t mesh_i = 0; mesh_i < mesh_items.size(); mesh_i++) {
		for (int32_t j = 0; j < mesh_items[mesh_i]->get_mesh()->get_surface_count(); j++) {
			r_model_vertices.push_back(Vector<ModelVertex>());
		}
	}
	uint32_t mesh_count = 0;
	for (int32_t mesh_i = 0; mesh_i < mesh_items.size(); mesh_i++) {
		for (int32_t j = 0; j < mesh_items[mesh_i]->get_mesh()->get_surface_count(); j++) {

			Array mesh = mesh_items[mesh_i]->get_mesh()->surface_get_arrays(j);
			if (mesh.empty()) {
				continue;
			}
			Array vertices = mesh[ArrayMesh::ARRAY_VERTEX];
			if (vertices.size() == 0) {
				continue;
			}
			Vector<Vector3> vertex_arr = mesh[Mesh::ARRAY_VERTEX];
			Vector<Vector3> normal_arr = mesh[Mesh::ARRAY_NORMAL];
			Vector<Vector2> uv_arr = mesh[Mesh::ARRAY_TEX_UV];
			Vector<Color> color_arr = mesh[Mesh::ARRAY_COLOR];
			Vector<int32_t> index_arr = mesh[Mesh::ARRAY_INDEX];
			Vector<Plane> tangent_arr = mesh[Mesh::ARRAY_TANGENT];
			Transform xform = original_mesh_items[mesh_i]->get_global_transform();
			Spatial *spatial_root = Object::cast_to<Spatial>(original_mesh_items[mesh_i]->get_owner());
			if (spatial_root) {
				xform = xform * spatial_root->get_transform().affine_inverse();
			}
			Vector<ModelVertex> model_vertices;
			model_vertices.resize(vertex_arr.size());
			for (int32_t vertex_i = 0; vertex_i < vertex_arr.size(); vertex_i++) {
				ModelVertex vertex;
				vertex.pos = xform.xform(vertex_arr[vertex_i]);
				if (normal_arr.size()) {
					vertex.normal = normal_arr[vertex_i];
				}
				if (uv_arr.size()) {
					vertex.uv = uv_arr[vertex_i];
				}
				if (color_arr.size()) {
					vertex.color = color_arr[vertex_i];
				}
				if (tangent_arr.size()) {
					vertex.tangent = tangent_arr[vertex_i];
				}
				model_vertices.write[vertex_i] = vertex;
			}
			r_model_vertices.write[mesh_count] = model_vertices;
			mesh_count++;
		}
	}
	mesh_count = 0;
	for (int32_t mesh_i = 0; mesh_i < mesh_items.size(); mesh_i++) {
		for (int32_t j = 0; j < mesh_items[mesh_i]->get_mesh()->get_surface_count(); j++) {
			Array mesh = mesh_items[mesh_i]->get_mesh()->surface_get_arrays(j);
			if (mesh.empty()) {
				continue;
			}
			Vector<Vector3> vertices = mesh[ArrayMesh::ARRAY_VERTEX];
			if (vertices.size() == 0) {
				continue;
			}
			Vector<Vector2> uvs;
			uvs.resize(vertices.size());
			for (uint32_t vertex_i = 0; vertex_i < vertices.size(); vertex_i++) {
				Ref<SpatialMaterial> empty_material;
				empty_material.instance();
				if (mesh_count >= r_vertex_to_material.size()) {
					break;
				}
				Array vertex_to_material = r_vertex_to_material[mesh_count];
				if (!vertex_to_material.size()) {
					continue;
				}
				if (vertex_i >= vertex_to_material.size()) {
					continue;
				}
				const Ref<Material> material = vertex_to_material.get(vertex_i);
				if (material.is_null()) {
					break;
				}
				if (!Object::cast_to<SpatialMaterial>(*material)) {
					continue;
				}
				ERR_CONTINUE(material->get_class_name() != empty_material->get_class_name());
				const Ref<Texture> tex = Object::cast_to<SpatialMaterial>(*material)->get_texture(SpatialMaterial::TextureParam::TEXTURE_ALBEDO);
				uvs.write[vertex_i] = r_model_vertices[mesh_count][vertex_i].uv;
				if (tex.is_valid()) {
					uvs.write[vertex_i].x *= (float)tex->get_width();
					uvs.write[vertex_i].y *= (float)tex->get_height();
				}
			}
			uv_groups.push_back(uvs);
			mesh_count++;
		}
	}
}
Ref<Image> MeshMergeMaterialRepack::dilate(Ref<Image> source_image) {
	Ref<Image> target_image = source_image->duplicate();
	target_image->convert(Image::FORMAT_RGBA8);
	Vector<uint8_t> pixels;
	int32_t height = target_image->get_size().y;
	int32_t width = target_image->get_size().x;
	const int32_t bytes_in_pixel = 4;
	pixels.resize(height * width * bytes_in_pixel);
	target_image->lock();
	for (int32_t y = 0; y < height; y++) {
		for (int32_t x = 0; x < width; x++) {
			int32_t pixel_index = x + (width * y);
			int32_t index = pixel_index * bytes_in_pixel;
			Color pixel = target_image->get_pixel(x, y);
			pixels.write[index + 0] = uint8_t(pixel.r * 255.0);
			pixels.write[index + 1] = uint8_t(pixel.g * 255.0);
			pixels.write[index + 2] = uint8_t(pixel.b * 255.0);
			pixels.write[index + 3] = uint8_t(pixel.a * 255.0);
		}
	}
	target_image->unlock();
	rjm_texbleed(pixels.ptrw(), width, height, 3, bytes_in_pixel, bytes_in_pixel * width);
	target_image->lock();
	for (int32_t y = 0; y < height; y++) {
		for (int32_t x = 0; x < width; x++) {
			Color pixel;
			int32_t pixel_index = x + (width * y);
			int32_t index = bytes_in_pixel * pixel_index;
			pixel.r = pixels[index + 0] / 255.0;
			pixel.g = pixels[index + 1] / 255.0;
			pixel.b = pixels[index + 2] / 255.0;
			pixel.a = pixels[index + 3] / 255.0;
			target_image->set_pixel(x, y, pixel);
		}
	}
	target_image->unlock();
	target_image->generate_mipmaps();
	return target_image;
}

void MeshMergeMaterialRepack::map_vertex_to_material(const Vector<MeshInstance *> mesh_items, Array &vertex_to_material, Vector<Ref<Material> > &material_cache) {
	for (int32_t mesh_i = 0; mesh_i < mesh_items.size(); mesh_i++) {
		for (int32_t j = 0; j < mesh_items[mesh_i]->get_mesh()->get_surface_count(); j++) {
			Array mesh = mesh_items[mesh_i]->get_mesh()->surface_get_arrays(j);
			Vector<Vector3> indices = mesh[ArrayMesh::ARRAY_INDEX];
			Array materials;
			materials.resize(indices.size());
			Ref<Material> mat = mesh_items[mesh_i]->get_mesh()->surface_get_material(j);
			if (mesh_items[mesh_i]->get_surface_material(j).is_valid()) {
				mat = mesh_items[mesh_i]->get_surface_material(j);
			}
			if (material_cache.find(mat) == -1) {
				material_cache.push_back(mat);
			}
			for (int32_t index_i = 0; index_i < indices.size(); index_i++) {
				if (mat.is_valid()) {
					materials[index_i] = mat;
				} else {
					Ref<SpatialMaterial> new_mat;
					new_mat.instance();
					materials[index_i] = new_mat;
				}
			}
			vertex_to_material.push_back(materials);
		}
	}
}

Node *MeshMergeMaterialRepack::_output(MergeState &state) {
	MeshMergeMaterialRepack::TextureData texture_data;
	for (int32_t mesh_i = 0; mesh_i < state.r_mesh_items.size(); mesh_i++) {
		if (state.r_mesh_items[mesh_i]->get_parent()) {
			Spatial *spatial = memnew(Spatial);
			Transform xform = state.r_mesh_items[mesh_i]->get_transform();
			spatial->set_transform(xform);
			spatial->set_name(state.r_mesh_items[mesh_i]->get_name());
			state.r_mesh_items[mesh_i]->replace_by(spatial);
		}
	}
	Ref<SurfaceTool> st_all;
	st_all.instance();
	st_all->begin(Mesh::PRIMITIVE_TRIANGLES);
	for (uint32_t mesh_i = 0; mesh_i < state.atlas->meshCount; mesh_i++) {
		Ref<SurfaceTool> st;
		st.instance();
		st->begin(Mesh::PRIMITIVE_TRIANGLES);
		const xatlas::Mesh &mesh = state.atlas->meshes[mesh_i];
		for (uint32_t v = 0; v < mesh.vertexCount; v++) {
			const xatlas::Vertex vertex = mesh.vertexArray[v];
			const ModelVertex &sourceVertex = state.model_vertices[mesh_i][vertex.xref];
			st->add_uv(Vector2(vertex.uv[0] / state.atlas->width, vertex.uv[1] / state.atlas->height));
			Vector3 normal = sourceVertex.normal;
			st->add_normal(normal);
			st->add_smooth_group(true);
			st->add_color(sourceVertex.color);
			st->add_tangent(sourceVertex.tangent);
			st->add_vertex(Vector3(sourceVertex.pos.x, sourceVertex.pos.y, sourceVertex.pos.z));
		}

		for (uint32_t f = 0; f < mesh.indexCount; f++) {
			const uint32_t index = mesh.indexArray[f];
			st->add_index(index);
		}
		Ref<ArrayMesh> array_mesh = st->commit();
		st_all->append_from(array_mesh, 0, Transform());
	}
	Ref<SpatialMaterial> mat;
	mat.instance();
	mat->set_name("Atlas");
	if (state.atlas->width != 0 || state.atlas->height != 0) {
		Map<String, Ref<Image> >::Element *A = state.texture_atlas.find("albedo");
		if (A && !A->get()->empty()) {
			Ref<ImageTexture> texture;
			texture.instance();
			Ref<Image> img = dilate(A->get());
			texture->create_from_image(img);
			texture->set_storage(ImageTexture::STORAGE_COMPRESS_LOSSY);
			if (state.is_transparent) {
				mat->set_feature(SpatialMaterial::FEATURE_TRANSPARENT, true);
				mat->set_depth_draw_mode(SpatialMaterial::DEPTH_DRAW_ALPHA_OPAQUE_PREPASS);
			}
			mat->set_texture(SpatialMaterial::TEXTURE_ALBEDO, texture);
		}
		Map<String, Ref<Image> >::Element *E = state.texture_atlas.find("emission");
		if (E && !E->get()->empty()) {
			Ref<ImageTexture> texture;
			texture.instance();
			Ref<Image> img = dilate(E->get());
			texture->create_from_image(img);
			texture->set_storage(ImageTexture::STORAGE_COMPRESS_LOSSY);
			mat->set_feature(SpatialMaterial::FEATURE_EMISSION, true);
			mat->set_texture(SpatialMaterial::TEXTURE_EMISSION, texture);
		}
		Map<String, Ref<Image> >::Element *N = state.texture_atlas.find("normal");
		if (N && !N->get()->empty()) {
			bool has_normals = false;
			N->get()->lock();
			for (int32_t y = 0; y < N->get()->get_height(); y++) {
				for (int32_t x = 0; x < N->get()->get_width(); x++) {
					Color texel = N->get()->get_pixel(x, y);
					if (texel != Color(0.0f, 0.0f, 0.0f, 0.0f)) {
						has_normals = has_normals || true;
					}
				}
			}
			N->get()->unlock();
			if (has_normals) {
				Ref<ImageTexture> texture;
				texture.instance();
				Ref<Image> img = dilate(N->get());
				texture->create_from_image(img);
				texture->set_storage(ImageTexture::STORAGE_COMPRESS_LOSSY);
				mat->set_feature(SpatialMaterial::FEATURE_NORMAL_MAPPING, true);
				mat->set_texture(SpatialMaterial::TEXTURE_NORMAL, texture);
			}
		}
		Map<String, Ref<Image> >::Element *ORM = state.texture_atlas.find("orm");
		if (ORM && !ORM->get()->empty()) {
			Ref<ImageTexture> texture;
			texture.instance();
			Ref<Image> img = dilate(ORM->get());
			texture->create_from_image(img);
			texture->set_storage(ImageTexture::STORAGE_COMPRESS_LOSSY);
			mat->set_ao_texture_channel(SpatialMaterial::TEXTURE_CHANNEL_RED);
			mat->set_feature(SpatialMaterial::FEATURE_AMBIENT_OCCLUSION, true);
			mat->set_texture(SpatialMaterial::TEXTURE_AMBIENT_OCCLUSION, texture);
			mat->set_roughness_texture_channel(SpatialMaterial::TEXTURE_CHANNEL_GREEN);
			mat->set_roughness(1.0f);
			mat->set_texture(SpatialMaterial::TEXTURE_ROUGHNESS, texture);
			mat->set_metallic_texture_channel(SpatialMaterial::TEXTURE_CHANNEL_BLUE);
			mat->set_metallic(1.0f);
			mat->set_texture(SpatialMaterial::TEXTURE_METALLIC, texture);
		}
	}
	MeshInstance *mi = memnew(MeshInstance);
	st_all->generate_tangents();
	Ref<ArrayMesh> array_mesh = st_all->commit();
	mi->set_mesh(array_mesh);
	mi->set_name(state.p_name + "Merged");
	print_line("Merged scene.");
	array_mesh->surface_set_material(0, mat);
	state.p_root->add_child(mi);
	mi->set_owner(state.p_root);
	return state.p_root;
}

#ifdef TOOLS_ENABLED
void SceneMergePlugin::merge(Variant p_user_data) {
	file_export_lib = memnew(EditorFileDialog);
	file_export_lib->set_title(TTR("Export Library"));
	file_export_lib->set_mode(EditorFileDialog::MODE_SAVE_FILE);
	file_export_lib->connect("file_selected", this, "_dialog_action");
	file_export_lib_merge = memnew(CheckBox);
	file_export_lib_merge->set_text(TTR("Merge With Existing"));
	file_export_lib_merge->set_pressed(false);
	file_export_lib->get_vbox()->add_child(file_export_lib_merge);
	editor->get_gui_base()->add_child(file_export_lib);
	List<String> extensions;
	extensions.push_back("tscn");
	extensions.push_back("scn");
	file_export_lib->clear_filters();
	for (int extension_i = 0; extension_i < extensions.size(); extension_i++) {
		file_export_lib->add_filter("*." + extensions[extension_i] + " ; " + extensions[extension_i].to_upper());
	}
	file_export_lib->popup_centered_ratio();
	file_export_lib->set_title(TTR("Merge Scene"));
}

void SceneMergePlugin::_dialog_action(String p_file) {
	Node *node = editor->get_tree()->get_edited_scene_root();
	if (!node) {
		editor->show_accept(TTR("This operation can't be done without a scene."), TTR("OK"));
		return;
	}
	if (FileAccess::exists(p_file) && file_export_lib_merge->is_pressed()) {
		Ref<PackedScene> scene = ResourceLoader::load(p_file, "PackedScene");
		if (scene.is_null()) {
			editor->show_accept(TTR("Can't load scene for merging!"), TTR("OK"));
			return;
		} else {
			node->add_child(scene->instance());
		}
	}
	scene_optimize->merge(p_file, node);
	EditorFileSystem::get_singleton()->scan_changes();
	file_export_lib->queue_delete();
	file_export_lib_merge->queue_delete();
}
void SceneMergePlugin::_bind_methods() {
	ClassDB::bind_method("_dialog_action", &SceneMergePlugin::_dialog_action);
	ClassDB::bind_method(D_METHOD("merge"), &SceneMergePlugin::merge);
}

void SceneMergePlugin::_notification(int notification) {
	if (notification == NOTIFICATION_ENTER_TREE) {
		editor->add_tool_menu_item("Merge Scene", this, "merge");
	} else if (notification == NOTIFICATION_EXIT_TREE) {
		editor->remove_tool_menu_item("Merge Scene");
	}
}

SceneMergePlugin::SceneMergePlugin(EditorNode *p_node) {
	editor = p_node;
}
#endif