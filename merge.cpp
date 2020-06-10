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

void SceneMerge::merge(const String p_file, Node *p_root_node) {
	PackedScene *scene = memnew(PackedScene);
	scene->pack(p_root_node);
	Node *root = scene->instance();
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
		int _width = args->sourceTexture->get_width() - 1;
		float sx = sourceUv.x * _width;
		while (sx < 0) {
			sx += _width;
		}
		if ((int32_t)sx > _width) {
			sx = Math::fmod(sx, _width);
		}
		int _height = args->sourceTexture->get_height() - 1;
		float sy = sourceUv.y * _height;
		while (sy < 0) {
			sy += _height;
		}
		if ((int32_t)sy > _height) {
			sy = Math::fmod(sy, _height);
		}
		const Color color = args->sourceTexture->get_pixel(sx, sy);
		args->atlasData->set_pixel(x, y, color);
		AtlasLookupTexel &lookup = args->atlas_lookup[x * y + args->atlas_width];
		lookup.material_index = args->material_index;
		lookup.x = (uint16_t)sx;
		lookup.y = (uint16_t)sy;
		return true;
	}
	return false;
}

void MeshMergeMaterialRepack::_find_all_mesh_instances(Vector<MeshState> &r_items, Node *p_current_node, const Node *p_owner) {
	MeshInstance *mi = Object::cast_to<MeshInstance>(p_current_node);
	if (mi) {
		Ref<ArrayMesh> array_mesh = mi->get_mesh();
		if (array_mesh.is_valid()) {
			bool has_blends = false;
			bool has_bones = false;
			bool has_transparency = false;
			for (int32_t surface_i = 0; surface_i < array_mesh->get_surface_count(); surface_i++) {

				Array array = array_mesh->surface_get_arrays(surface_i);
				Array bones = array[ArrayMesh::ARRAY_BONES];
				has_bones |= bones.size() != 0;
				has_blends |= array_mesh->get_blend_shape_count() != 0;
				Ref<SpatialMaterial> spatial_mat = array_mesh->surface_get_material(surface_i);
				if (spatial_mat.is_valid()) {
					Ref<Image> albedo_img = spatial_mat->get_texture(SpatialMaterial::TEXTURE_ALBEDO);
					has_transparency |= spatial_mat->get_feature(SpatialMaterial::FEATURE_TRANSPARENT) || spatial_mat->get_flag(SpatialMaterial::FLAG_USE_ALPHA_SCISSOR);
				}
				if (has_blends || has_bones || has_transparency) {
					break;
				}
			}
			if (!has_blends && !has_bones && !has_transparency) {
				for (int32_t surface_i = 0; surface_i < array_mesh->get_surface_count(); surface_i++) {
					Array array = array_mesh->surface_get_arrays(surface_i);
					Array bones = array[ArrayMesh::ARRAY_BONES];
					Array uvs = array[ArrayMesh::ARRAY_TEX_UV];
					has_bones |= bones.size() != 0;
					has_blends |= array_mesh->get_blend_shape_count() != 0;
					Ref<SpatialMaterial> spatial_mat = array_mesh->surface_get_material(surface_i);
					if (spatial_mat.is_valid()) {
						Ref<Image> albedo_img = spatial_mat->get_texture(SpatialMaterial::TEXTURE_ALBEDO);
						has_transparency |= spatial_mat->get_feature(SpatialMaterial::FEATURE_TRANSPARENT) || spatial_mat->get_flag(SpatialMaterial::FLAG_USE_ALPHA_SCISSOR);
					}
					if (!has_blends && !has_bones && !has_transparency) {
						MeshState mesh_state;
						Ref<SurfaceTool> st;
						st.instance();
						st->create_from_triangle_arrays(array);
						Ref<ArrayMesh> split_mesh = st->commit();
						split_mesh->surface_set_material(0, array_mesh->surface_get_material(surface_i));
						mesh_state.mesh = split_mesh;
						if (mi->is_inside_tree()) {
							mesh_state.path = mi->get_path();
						}
						mesh_state.mesh_instance = mi;
						r_items.push_back(mesh_state);
					}
				}
			}
		}
	}
	for (int32_t child_i = 0; child_i < p_current_node->get_child_count(); child_i++) {
		_find_all_mesh_instances(r_items, p_current_node->get_child(child_i), p_owner);
	}
}

void MeshMergeMaterialRepack::_find_all_animated_meshes(Vector<MeshState> &r_items, Node *p_current_node, const Node *p_owner) {
	AnimationPlayer *ap = Object::cast_to<AnimationPlayer>(p_current_node);
	if (ap) {
		List<StringName> animation_names;
		ap->get_animation_list(&animation_names);
		Map<String, MeshState> paths;
		for (int32_t i = 0; i < r_items.size(); i++) {
			MeshInstance *mi = r_items[i].mesh_instance;
			String path = ap->get_parent()->get_path_to(mi);
			paths.insert(path, r_items[i]);
		}
		for (int32_t anim_i = 0; anim_i < animation_names.size(); anim_i++) {
			Ref<Animation> anim = ap->get_animation(animation_names[anim_i]);
			for (Map<String, MeshState>::Element *E = paths.front(); E; E = E->next()) {
				String path = E->key();
				for (int32_t track_i = 0; track_i < anim->get_track_count(); track_i++) {
					NodePath anim_path = anim->track_get_path(track_i);
					String anim_path_string = anim_path;
					if (path.begins_with(anim_path_string) && r_items.find(E->get()) != -1) {
						r_items.erase(E->get());
					}
				}
			}
		}
	}
	for (int32_t child_i = 0; child_i < p_current_node->get_child_count(); child_i++) {
		_find_all_animated_meshes(r_items, p_current_node->get_child(child_i), p_owner);
	}
}

Node *MeshMergeMaterialRepack::merge(Node *p_root, Node *p_original_root) {
	Vector<MeshState> mesh_items;
	_find_all_mesh_instances(mesh_items, p_root, p_root);
	_find_all_animated_meshes(mesh_items, p_root, p_root);

	Vector<MeshState> original_mesh_items;
	_find_all_mesh_instances(original_mesh_items, p_original_root, p_original_root);
	_find_all_animated_meshes(original_mesh_items, p_original_root, p_original_root);
	if (!original_mesh_items.size()) {
		return p_root;
	}
	Array mesh_to_index_to_material;
	Vector<Ref<Material> > material_cache;
	Ref<Material> empty_material;
	material_cache.push_back(empty_material);
	map_mesh_to_index_to_material(mesh_items, mesh_to_index_to_material, material_cache);

	Vector<Vector<Vector2> > uv_groups;
	Vector<Vector<ModelVertex> > model_vertices;
	scale_uvs_by_texture_dimension(original_mesh_items, mesh_items, uv_groups, mesh_to_index_to_material, model_vertices);
	xatlas::SetPrint(printf, true);
	xatlas::Atlas *atlas = xatlas::Create();

	int32_t num_surfaces = 0;
	for (int32_t mesh_i = 0; mesh_i < mesh_items.size(); mesh_i++) {
		for(int32_t j = 0; j < mesh_items[mesh_i].mesh->get_surface_count(); j++) {
			Array mesh = mesh_items[mesh_i].mesh->surface_get_arrays(j);
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
	_generate_atlas(num_surfaces, uv_groups, atlas, mesh_items, material_cache, pack_options);
	atlas_lookup.resize(atlas->width * atlas->height);
	Map<String, Ref<Image> > texture_atlas;

	MergeState state = { p_root, atlas, mesh_items, mesh_to_index_to_material, uv_groups, model_vertices, p_root->get_name(), pack_options, atlas_lookup, material_cache, texture_atlas };

	EditorProgress progress_scene_merge("gen_get_source_material", TTR("Get source material"), state.material_cache.size());
	int step = 0;
	for (int32_t material_cache_i = 0; material_cache_i < state.material_cache.size(); material_cache_i++) {
		step++;
		Ref<SpatialMaterial> material = state.material_cache[material_cache_i];
		if (material.is_null()) {
			continue;
		}
		if (material->get_texture(SpatialMaterial::TEXTURE_ALBEDO).is_null()) {
			Ref<Image> img;
			img.instance();
			img->create(default_texture_length, default_texture_length, false, Image::FORMAT_RGBA8);
			img->fill(material->get_albedo());
			material->set_albedo(Color(1.0f, 1.0f, 1.0f));
			Ref<ImageTexture> tex;
			tex.instance();
			tex->create_from_image(img);
			material->set_texture(SpatialMaterial::TEXTURE_ALBEDO, tex);
		}
		if (material->get_texture(SpatialMaterial::TEXTURE_EMISSION).is_null()) {
			Ref<Image> img;
			img.instance();
			img->create(default_texture_length, default_texture_length, false, Image::FORMAT_RGBA8);
			img->fill(material->get_emission());

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
			material->set_feature(SpatialMaterial::FEATURE_EMISSION, true);
			Color c;
			c.r = c.r * color_mul.r + color_add.r;
			c.g = c.g * color_mul.g + color_add.g;
			c.b = c.b * color_mul.b + color_add.b;
			material->set_emission(c);
			Ref<ImageTexture> tex;
			tex.instance();
			tex->create_from_image(img);
			material->set_texture(SpatialMaterial::TEXTURE_EMISSION, tex);
		}
		if (material->get_texture(SpatialMaterial::TEXTURE_ROUGHNESS).is_null()) {
			Ref<Image> img;
			img.instance();
			img->create(default_texture_length, default_texture_length, false, Image::FORMAT_RGBA8);
			float roughness = material->get_roughness();
			Color c = Color(roughness, roughness, roughness);
			material->set_roughness(1.0f);
			img->fill(c);
			Ref<ImageTexture> tex;
			tex.instance();
			tex->create_from_image(img);
			material->set_roughness_texture_channel(SpatialMaterial::TEXTURE_CHANNEL_GREEN);
			material->set_texture(SpatialMaterial::TEXTURE_ROUGHNESS, tex);
		}
		if (material->get_texture(SpatialMaterial::TEXTURE_METALLIC).is_null()) {
			Ref<Image> img;
			img.instance();
			img->create(default_texture_length, default_texture_length, false, Image::FORMAT_RGBA8);
			float metallic = material->get_metallic();
			Color c = Color(metallic, metallic, metallic);
			material->set_metallic(1.0f);
			img->fill(c);
			Ref<ImageTexture> tex;
			tex.instance();
			tex->create_from_image(img);
			material->set_metallic_texture_channel(SpatialMaterial::TEXTURE_CHANNEL_GREEN);
			material->set_texture(SpatialMaterial::TEXTURE_METALLIC, tex);
		}
		if (material->get_texture(SpatialMaterial::TEXTURE_AMBIENT_OCCLUSION).is_null()) {
			Ref<Image> img;
			img.instance();
			img->create(default_texture_length, default_texture_length, false, Image::FORMAT_RGBA8);
			float ao = 1.0f;
			Color c = Color(ao, ao, ao);
			img->fill(c);
			Ref<ImageTexture> tex;
			tex.instance();
			tex->create_from_image(img);
			material->set_ao_texture_channel(SpatialMaterial::TEXTURE_CHANNEL_GREEN);
			material->set_texture(SpatialMaterial::TEXTURE_AMBIENT_OCCLUSION, tex);
		}
		MaterialImageCache cache;
		cache.albedo_img = _get_source_texture(state, material, "albedo");
		cache.normal_img = _get_source_texture(state, material, "normal");
		cache.orm_img = _get_source_texture(state, material, "orm");
		cache.emission_img = _get_source_texture(state, material, "emission");
		state.material_image_cache[material_cache_i] = cache;	
		progress_scene_merge.step(TTR("Getting Source Material: ") + material->get_name() + " (" + itos(step) + "/" + itos(state.material_cache.size()) + ")", step);
	}
	_generate_texture_atlas(state, "albedo");
	_generate_texture_atlas(state, "emission");
	_generate_texture_atlas(state, "normal");
	_generate_texture_atlas(state, "orm");
	_generate_texture_atlas(state, "emission");
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

	EditorProgress progress_texture_atlas("gen_mesh_atlas", TTR("Generate Atlas"), state.atlas->meshCount);
	int step = 0;
	for (uint32_t mesh_i = 0; mesh_i < state.atlas->meshCount; mesh_i++) {
		const xatlas::Mesh &mesh = state.atlas->meshes[mesh_i];
		for (uint32_t chart_i = 0; chart_i < mesh.chartCount; chart_i++) {
			const xatlas::Chart &chart = mesh.chartArray[chart_i];
			Ref<Image> img;
			if (texture_type == "albedo") {
				img = state.material_image_cache[chart.material].albedo_img;
			} else if (texture_type == "normal") {
				img = state.material_image_cache[chart.material].normal_img;
			} else if (texture_type == "orm") {
				img = state.material_image_cache[chart.material].orm_img;
			} else if (texture_type == "emission") {
				img = state.material_image_cache[chart.material].emission_img;
			}
			ERR_CONTINUE(img.is_null());
			ERR_CONTINUE_MSG(Image::get_format_pixel_size(img->get_format()) > 4, "Float textures are not supported yet");
			img->convert(Image::FORMAT_RGBA8);
			SetAtlasTexelArgs args;
			args.sourceTexture = img;
			args.atlasData = atlas_img;
			args.atlas_lookup = state.atlas_lookup.ptrw();
			args.material_index = (uint16_t)chart.material;
			for (uint32_t face_i = 0; face_i < chart.faceCount; face_i++) {
				Vector2 v[3];
				for (uint32_t l = 0; l < 3; l++) {
					const uint32_t index = mesh.indexArray[chart.faceArray[face_i] * 3 + l];
					const xatlas::Vertex &vertex = mesh.vertexArray[index];
					v[l] = Vector2(vertex.uv[0], vertex.uv[1]);
					args.source_uvs[l].x = state.uvs[mesh_i][vertex.xref].x / MAX(texture_minimum_side, img->get_width());
					args.source_uvs[l].y = state.uvs[mesh_i][vertex.xref].y / MAX(texture_minimum_side, img->get_height());
				}
				Triangle tri(v[0], v[1], v[2], Vector3(1, 0, 0), Vector3(0, 1, 0), Vector3(0, 0, 1));

				args.sourceTexture->lock();
				args.atlasData->lock();
				tri.drawAA(setAtlasTexel, &args);
				args.sourceTexture->unlock();
				args.atlasData->unlock();
			}
		}
		progress_texture_atlas.step(TTR("Process Mesh for Atlas: ") + texture_type + " (" + itos(step) + "/" + itos(state.atlas->meshCount) + ")", step);
		step++;
	}
	state.texture_atlas.insert(texture_type, atlas_img);
}

Ref<Image> MeshMergeMaterialRepack::_get_source_texture(MergeState &state, const Ref<SpatialMaterial> material, String texture_type) {
	int32_t width = texture_minimum_side;
	int32_t height = texture_minimum_side;
	if (material.is_null()) {
		Ref<Image> img;
		img.instance();
		img->create(width, height, false, Image::FORMAT_RGBA8);
		return img;
	}
	Ref<Texture> ao_texture = material->get_texture(SpatialMaterial::TEXTURE_AMBIENT_OCCLUSION);
	Ref<Image> ao_img;
	if (ao_texture.is_valid()) {
		ao_img = ao_texture->get_data();
	}
	Ref<Texture> metallic_texture = material->get_texture(SpatialMaterial::TEXTURE_METALLIC);
	Ref<Image> metallic_img;
	if (metallic_texture.is_valid()) {
		metallic_img = metallic_texture->get_data();
	}
	Ref<Texture> roughness_texture = material->get_texture(SpatialMaterial::TEXTURE_ROUGHNESS);
	Ref<Image> roughness_img;
	if (roughness_texture.is_valid()) {
		roughness_img = roughness_texture->get_data();
	}
	Ref<Texture> albedo_texture = material->get_texture(SpatialMaterial::TEXTURE_ALBEDO);
	Ref<Image> albedo_img;
	if (albedo_texture.is_valid()) {
		albedo_img = albedo_texture->get_data();
	}
	Ref<Texture> normal_texture = material->get_texture(SpatialMaterial::TEXTURE_NORMAL);
	Ref<Image> normal_img;
	if (normal_texture.is_valid()) {
		normal_img = normal_texture->get_data();
	}
	Ref<Texture> emission_texture = material->get_texture(SpatialMaterial::TEXTURE_EMISSION);
	Ref<Image> emission_img;
	if (albedo_img.is_valid() && !albedo_img->empty()) {
		width = MAX(width, albedo_img->get_width());
		height = MAX(height, albedo_img->get_height());
	}
	if (emission_texture.is_valid()) {
		emission_img = emission_texture->get_data();
	}
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
	if (emission_img.is_valid() && !emission_img->empty()) {
		width = MAX(width, emission_img->get_width());
		height = MAX(height, emission_img->get_height());
	}
	if (normal_img.is_valid() && !normal_img->empty()) {
		width = MAX(width, normal_img->get_width());
		height = MAX(height, normal_img->get_height());
	}
	if (albedo_img.is_valid()) {
		if (!albedo_img->empty()) {
			if (albedo_img->is_compressed()) {
				albedo_img->decompress();
			}
		}
		albedo_img->resize(width, height, Image::INTERPOLATE_LANCZOS);
	}
	if (ao_img.is_valid()) {
		if (!ao_img->empty()) {
			if (ao_img->is_compressed()) {
				ao_img->decompress();
			}
		}
		ao_img->resize(width, height, Image::INTERPOLATE_LANCZOS);
	}
	if (roughness_img.is_valid()) {
		if (!roughness_img->empty()) {
			if (roughness_img->is_compressed()) {
				roughness_img->decompress();
			}
		}
		roughness_img->resize(width, height, Image::INTERPOLATE_LANCZOS);
	}
	if (metallic_img.is_valid()) {
		if (!metallic_img->empty()) {
			if (metallic_img->is_compressed()) {
				metallic_img->decompress();
			}
		}
		metallic_img->resize(width, height, Image::INTERPOLATE_LANCZOS);
	}
	if (normal_img.is_valid()) {
		if (!normal_img->empty()) {
			if (normal_img->is_compressed()) {
				normal_img->decompress();
			}
		}
		normal_img->resize(width, height, Image::INTERPOLATE_LANCZOS);
	}
	if (emission_img.is_valid()) {
		if (!emission_img->empty()) {
			if (emission_img->is_compressed()) {
				emission_img->decompress();
			}
		}
		emission_img->resize(width, height, Image::INTERPOLATE_LANCZOS);
	}
	Ref<Image> img;
	img.instance();
	img->create(width, height, false, Image::FORMAT_RGBA8);
	Ref<Texture> tex;
	if (texture_type == "orm") {
		img->create(width, height, false, Image::FORMAT_RGB8);
		for (int32_t y = 0; y < img->get_height(); y++) {
			for (int32_t x = 0; x < img->get_width(); x++) {
				Color orm;
				if (ao_img.is_valid() && !ao_img->empty()) {
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
				float channel_mul = 0.0f;
				if (roughness_img.is_valid() && !roughness_img->empty()) {
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
				if (roughness_img.is_valid()) {
					channel_mul = material->get_roughness();
					orm.g = orm.g * channel_mul;
				} else {
					orm.g = material->get_roughness();
				}
				if (metallic_img.is_valid() && !metallic_img->empty()) {
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
				if (metallic_img.is_valid()) {
					channel_mul = material->get_metallic();
					orm.b = orm.b * channel_mul;
				} else {
					orm.b = material->get_metallic();
				}
				img->lock();
				img->set_pixel(x, y, orm);
				img->unlock();
			}
		}
	} else if (texture_type == "albedo") {
		Color color_mul;
		Color color_add;
		if (albedo_img.is_valid()) {
			color_mul = material->get_albedo();
			color_add = Color(0, 0, 0, 0);
			albedo_img->lock();
		} else {
			color_mul = Color(0, 0, 0, 0);
			color_add = material->get_albedo();
		}
		img->lock();
		for (int32_t y = 0; y < img->get_height(); y++) {
			for (int32_t x = 0; x < img->get_width(); x++) {
				Color c;
				if (albedo_img.is_valid()) {
					c = albedo_img->get_pixel(x, y);
				}
				c.r = c.r * color_mul.r + color_add.r;
				c.g = c.g * color_mul.g + color_add.g;
				c.b = c.b * color_mul.b + color_add.b;
				c.a = c.a * color_mul.a + color_add.a;
				img->set_pixel(x, y, c);
			}
		}
		if (albedo_img.is_valid()) {
			albedo_img->unlock();
		}
		img->unlock();
	} else if (texture_type == "normal") {
		if (normal_img.is_valid()) {
			img = normal_img;
		}
	} else if (texture_type == "emission") {
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
		emission_img->lock();
		img->lock();
		for (int32_t y = 0; y < img->get_height(); y++) {
			for (int32_t x = 0; x < img->get_width(); x++) {
				Color c = emission_img->get_pixel(x, y);
				c.r = c.r * color_mul.r + color_add.r;
				c.g = c.g * color_mul.g + color_add.g;
				c.b = c.b * color_mul.b + color_add.b;
				img->set_pixel(x, y, c);
			}
		}
		emission_img->unlock();
		img->unlock();
	}
	return img;
}

void MeshMergeMaterialRepack::_generate_atlas(const int32_t p_num_meshes, Vector<Vector<Vector2> > &r_uvs, xatlas::Atlas *atlas, const Vector<MeshState> &r_meshes, const Vector<Ref<Material> > material_cache,
		xatlas::PackOptions &pack_options) {
	uint32_t mesh_count = 0;
	for (int32_t mesh_i = 0; mesh_i < r_meshes.size(); mesh_i++) {
		for (int32_t j = 0; j < r_meshes[mesh_i].mesh->get_surface_count(); j++) {
			Array mesh = r_meshes[mesh_i].mesh->surface_get_arrays(j);
			if (mesh.empty()) {
				continue;
			}
			Array indices = mesh[ArrayMesh::ARRAY_INDEX];
			xatlas::UvMeshDecl meshDecl;
			meshDecl.vertexCount = r_uvs[mesh_count].size();
			meshDecl.vertexUvData = r_uvs[mesh_count].ptr();
			meshDecl.vertexStride = sizeof(Vector2);
			Vector<int32_t> mesh_indices = mesh[Mesh::ARRAY_INDEX];
			Vector<uint32_t> indexes;
			indexes.resize(mesh_indices.size());
			Vector<uint32_t> materials;
			materials.resize(mesh_indices.size());
			for (int32_t index_i = 0; index_i < mesh_indices.size(); index_i++) {
				indexes.write[index_i] = mesh_indices[index_i];
			}
			for (int32_t index_i = 0; index_i < mesh_indices.size(); index_i++) {
				Ref<Material> mat = r_meshes[mesh_i].mesh->surface_get_material(j);
				int32_t material_i = material_cache.find(mat);
				if (material_i != -1) {
					materials.write[index_i] = material_i;
				}
			}
			meshDecl.indexCount = indexes.size();
			meshDecl.indexData = indexes.ptr();
			meshDecl.indexFormat = xatlas::IndexFormat::UInt32;
			meshDecl.indexOffset = 0;
			meshDecl.faceMaterialData = materials.ptr();
			meshDecl.rotateCharts = false;
			xatlas::AddMeshError::Enum error = xatlas::AddUvMesh(atlas, meshDecl);
			if (error != xatlas::AddMeshError::Success) {
				ERR_CONTINUE_MSG(error != xatlas::AddMeshError::Success, "Error adding mesh %d: %s\n" + itos(mesh_i) + xatlas::StringForEnum(error));
			}
			mesh_count++;
		}
	}
	pack_options.bilinear = false;
	pack_options.padding = 16;
	pack_options.texelsPerUnit = 0.8f;
	pack_options.blockAlign = true;
	xatlas::PackCharts(atlas, pack_options);
}

void MeshMergeMaterialRepack::scale_uvs_by_texture_dimension(const Vector<MeshState> &original_mesh_items, Vector<MeshState> &mesh_items, Vector<Vector<Vector2> > &uv_groups, Array &r_mesh_to_index_to_material, Vector<Vector<ModelVertex> > &r_model_vertices) {
	for (int32_t mesh_i = 0; mesh_i < mesh_items.size(); mesh_i++) {
		for (int32_t j = 0; j < mesh_items[mesh_i].mesh->get_surface_count(); j++) {
			r_model_vertices.push_back(Vector<ModelVertex>());
		}
	}
	int32_t mesh_count = 0;
	for (int32_t mesh_i = 0; mesh_i < mesh_items.size(); mesh_i++) {
		for (int32_t surface_i = 0; surface_i < mesh_items[mesh_i].mesh->get_surface_count(); surface_i++) {
			Ref<ArrayMesh> array_mesh = mesh_items[mesh_i].mesh;
			Array mesh = array_mesh->surface_get_arrays(surface_i);
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
			Vector<int32_t> index_arr = mesh[Mesh::ARRAY_INDEX];
			Vector<Plane> tangent_arr = mesh[Mesh::ARRAY_TANGENT];
			Transform xform = original_mesh_items[mesh_i].mesh_instance->get_global_transform();
			Vector<ModelVertex> model_vertices;
			model_vertices.resize(vertex_arr.size());
			for (int32_t vertex_i = 0; vertex_i < vertex_arr.size(); vertex_i++) {
				ModelVertex vertex;
				vertex.pos = xform.xform(vertex_arr[vertex_i]);
				if (normal_arr.size()) {
					Vector3 normal = normal_arr[vertex_i];
					vertex.normal = xform.basis.xform(normal).normalized();
				}
				if (uv_arr.size()) {
					vertex.uv = uv_arr[vertex_i];
				}
				model_vertices.write[vertex_i] = vertex;
			}
			r_model_vertices.write[mesh_count] = model_vertices;
			mesh_count++;
		}
	}
	mesh_count = 0;
	for (int32_t mesh_i = 0; mesh_i < mesh_items.size(); mesh_i++) {
		for (int32_t j = 0; j < mesh_items[mesh_i].mesh->get_surface_count(); j++) {
			Array mesh = mesh_items[mesh_i].mesh->surface_get_arrays(j);
			if (mesh.empty()) {
				continue;
			}
			Vector<Vector3> vertices = mesh[ArrayMesh::ARRAY_VERTEX];
			if (vertices.size() == 0) {
				continue;
			}
			Vector<Vector2> uvs;
			uvs.resize(vertices.size());
			Vector<int32_t> indices = mesh[ArrayMesh::ARRAY_INDEX];
			for (int32_t vertex_i = 0; vertex_i < vertices.size(); vertex_i++) {
				if (mesh_count >= r_mesh_to_index_to_material.size()) {
					uvs.resize(0);
					break;
				}
				Array index_to_material = r_mesh_to_index_to_material[mesh_count];
				if (!index_to_material.size()) {
					continue;
				}
				int32_t index = indices.find(vertex_i);
				if (index >= index_to_material.size()) {
					continue;
				}
				ERR_CONTINUE(index == -1);
				const Ref<Material> material = index_to_material.get(index);
				if (material.is_null()) {
					uvs.resize(0);
					break;
				}
				Ref<SpatialMaterial> spatial_material = material;
				if (spatial_material.is_null()) {
					continue;
				}
				const Ref<Texture> tex = spatial_material->get_texture(SpatialMaterial::TextureParam::TEXTURE_ALBEDO);
				uvs.write[vertex_i] = r_model_vertices[mesh_count][vertex_i].uv;
				if (tex.is_valid()) {
					uvs.write[vertex_i].x *= (float)MAX(texture_minimum_side, tex->get_width());
					uvs.write[vertex_i].y *= (float)MAX(texture_minimum_side, tex->get_height());
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
			pixel.a = 1.0f;
			target_image->set_pixel(x, y, pixel);
		}
	}
	target_image->unlock();
	target_image->generate_mipmaps();
	return target_image;
}

void MeshMergeMaterialRepack::map_mesh_to_index_to_material(const Vector<MeshState> mesh_items, Array &mesh_to_index_to_material, Vector<Ref<Material> > &material_cache) {
	for (int32_t mesh_i = 0; mesh_i < mesh_items.size(); mesh_i++) {
		Ref<ArrayMesh> array_mesh = mesh_items[mesh_i].mesh;
		for (int32_t j = 0; j < array_mesh->get_surface_count(); j++) {
			Array mesh = array_mesh->surface_get_arrays(j);
			Array uvs = mesh[ArrayMesh::ARRAY_TEX_UV];
			if (!uvs.size()) {
				array_mesh->mesh_unwrap(Transform(), 2.0f);
				break;
			}
		}
		for (int32_t j = 0; j < array_mesh->get_surface_count(); j++) {			
			Array mesh = array_mesh->surface_get_arrays(j);
			Vector<Vector3> indices = mesh[ArrayMesh::ARRAY_INDEX];
			Ref<Material> mat = mesh_items[mesh_i].mesh->surface_get_material(j);
			if (mesh_items[mesh_i].mesh_instance->get_surface_material(j).is_valid()) {
				mat = mesh_items[mesh_i].mesh_instance->get_surface_material(j);
			}
			if (material_cache.find(mat) == -1) {
				material_cache.push_back(mat);
			}
			Array materials;
			materials.resize(indices.size());
			for (int32_t index_i = 0; index_i < indices.size(); index_i++) {
				materials[index_i] = mat;
			}
			mesh_to_index_to_material.push_back(materials);
		}
	}
}

Node *MeshMergeMaterialRepack::_output(MergeState &state) {
	MeshMergeMaterialRepack::TextureData texture_data;
	for (int32_t mesh_i = 0; mesh_i < state.r_mesh_items.size(); mesh_i++) {
		if (state.r_mesh_items[mesh_i].mesh_instance->get_parent()) {
			Spatial *spatial = memnew(Spatial);
			Transform xform = state.r_mesh_items[mesh_i].mesh_instance->get_transform();
			spatial->set_transform(xform);
			spatial->set_name(state.r_mesh_items[mesh_i].mesh_instance->get_name());
			state.r_mesh_items[mesh_i].mesh_instance->replace_by(spatial);
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
			st->add_normal(sourceVertex.normal);
			st->add_color(Color(1.0f, 1.0f, 1.0f));
			st->add_vertex(sourceVertex.pos);
		}
		for (uint32_t f = 0; f < mesh.indexCount; f++) {
			const uint32_t index = mesh.indexArray[f];
			st->add_index(index);
		}
		st->generate_tangents();
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
				texture->create_from_image(N->get());
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
			mat->set_cull_mode(SpatialMaterial::CULL_DISABLED);
			mat->set_ao_texture_channel(SpatialMaterial::TEXTURE_CHANNEL_RED);
			mat->set_feature(SpatialMaterial::FEATURE_AMBIENT_OCCLUSION, true);
			mat->set_texture(SpatialMaterial::TEXTURE_AMBIENT_OCCLUSION, texture);
			mat->set_roughness_texture_channel(SpatialMaterial::TEXTURE_CHANNEL_GREEN);
			mat->set_texture(SpatialMaterial::TEXTURE_ROUGHNESS, texture);
			mat->set_metallic_texture_channel(SpatialMaterial::TEXTURE_CHANNEL_BLUE);
			mat->set_metallic(1.0);
			mat->set_texture(SpatialMaterial::TEXTURE_METALLIC, texture);
		}
	}
	MeshInstance *mi = memnew(MeshInstance);
	Ref<ArrayMesh> array_mesh = st_all->commit();
	mi->set_mesh(array_mesh);
	mi->set_name(state.p_name);
	Transform root_xform;
	Spatial *spatial = Object::cast_to<Spatial>(state.p_root);
	if (spatial) {
		root_xform = spatial->get_transform();
	}
	mi->set_transform(root_xform.affine_inverse());
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
	Node *root = editor->get_tree()->get_edited_scene_root();
	String filename = String(root->get_filename().get_file().get_basename());
	if (filename.empty()) {
		filename = root->get_name();
	}
	file_export_lib->set_current_file(filename + String(".scn"));
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
