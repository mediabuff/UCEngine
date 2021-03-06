#pragma once

#include <tuple>
#include <uc_dev/gx/error.h>
#include <uc_dev/math/math.h>


#include <uc_dev/gx/import/fbx/fbx_common.h>
#include <uc_dev/gx/import/fbx/fbx_transform.h>
#include <uc_dev/gx/import/fbx/fbx_transform_helper.h>

namespace uc
{
    namespace gx
    {
        namespace import
        {
            namespace fbx
            {
                struct get_uv_element
                {
                    virtual fbxsdk::FbxVector2 get_element(uint32_t index) const = 0;
                };

                struct get_uv_element_direct : public get_uv_element
                {
                    const fbxsdk::FbxGeometryElementUV* m_uv;

                public:

                    get_uv_element_direct(const FbxGeometryElementUV* uv) : m_uv(uv)
                    {

                    }

                    virtual fbxsdk::FbxVector2 get_element(uint32_t control_point_index) const override
                    {
                        return m_uv->GetDirectArray().GetAt(control_point_index);
                    }
                };

                struct get_uv_element_index_to_direct : public get_uv_element
                {
                    const fbxsdk::FbxGeometryElementUV* m_uv;

                public:

                    get_uv_element_index_to_direct(const FbxGeometryElementUV* uv) : m_uv(uv)
                    {

                    }

                    virtual fbxsdk::FbxVector2 get_element(uint32_t control_point_index) const override
                    {
                        auto id = m_uv->GetIndexArray().GetAt(control_point_index);
                        return m_uv->GetDirectArray().GetAt(id);
                    }
                };

                struct get_uv_point_index
                {
                    virtual uint32_t get_element(uint32_t triangle_index, uint32_t triangle_vertex) const = 0;
                };

                struct get_uv_control_point_index : public get_uv_point_index
                {
                    int* m_control_points;

                public:
                    get_uv_control_point_index(int* control_points) : m_control_points(control_points)
                    {

                    }

                    virtual uint32_t get_element(uint32_t triangle_index, uint32_t triangle_vertex) const override
                    {
                        assert(triangle_vertex == 0 || triangle_vertex == 1 || triangle_vertex == 2);
                        return m_control_points[triangle_index * 3 + triangle_vertex];
                    }
                };

                struct get_uv_texture_index : public get_uv_point_index
                {
                    const fbxsdk::FbxMesh* m_mesh;
                public:

                    get_uv_texture_index(const fbxsdk::FbxMesh* m) : m_mesh(m)
                    {

                    }

                    virtual uint32_t get_element(uint32_t triangle_index, uint32_t triangle_vertex) const override
                    {
                        auto m = const_cast<fbxsdk::FbxMesh*>(m_mesh);
                        return m->GetTextureUVIndex(triangle_index, triangle_vertex);
                    }
                };

                inline geo::indexed_mesh::faces_t get_faces(const fbxsdk::FbxMesh* mesh, const std::array<int32_t, 3> & p)
                {
                    auto triangle_count = mesh->GetPolygonCount();
                    geo::indexed_mesh::faces_t faces;
                    faces.reserve(triangle_count);

                    geo::indexed_mesh::positions_t positions;

                    for (auto triangle = 0U; triangle < static_cast<uint32_t>(triangle_count); ++triangle)
                    {
                        //reorient triangles ccw, since they come cw from fbx
                        geo::indexed_mesh::face_t face = { triangle * 3 + p[0] , triangle * 3 + p[1], triangle * 3 + p[2] };
                        faces.push_back(face);
                    }

                    return faces;
                }

                //returns all positions of an fbx sdk mesh
                inline geo::indexed_mesh::positions_t get_positions(const fbxsdk::FbxMesh* mesh)
                {
                    auto points = mesh->GetControlPoints();
                    auto indices = mesh->GetPolygonVertices();

                    auto triangle_count = mesh->GetPolygonCount();
                    geo::indexed_mesh::positions_t positions;

                    auto node_transform = world_transform(mesh->GetNode());

                    for (auto triangle = 0; triangle < triangle_count; ++triangle)
                    {
                        auto i0 = indices[triangle * 3];
                        auto i1 = indices[triangle * 3 + 1];
                        auto i2 = indices[triangle * 3 + 2];

                        //positions
                        {
                            const double* v0 = points[i0];
                            const double* v1 = points[i1];
                            const double* v2 = points[i2];

                            math::float4  vr0 = math::set(static_cast<float>(v0[0]), static_cast<float>(v0[1]), static_cast<float>(v0[2]), 1.0f);
                            math::float4  vr1 = math::set(static_cast<float>(v1[0]), static_cast<float>(v1[1]), static_cast<float>(v1[2]), 1.0f);
                            math::float4  vr2 = math::set(static_cast<float>(v2[0]), static_cast<float>(v2[1]), static_cast<float>(v2[2]), 1.0f);

                            math::float4  vq0 = math::mul(vr0, node_transform);
                            math::float4  vq1 = math::mul(vr1, node_transform);
                            math::float4  vq2 = math::mul(vr2, node_transform);

                            geo::indexed_mesh::position_t vp0;
                            geo::indexed_mesh::position_t vp1;
                            geo::indexed_mesh::position_t vp2;

                            math::store3u(&vp0.x, vq0);
                            math::store3u(&vp1.x, vq1);
                            math::store3u(&vp2.x, vq2);

                            positions.push_back(vp0);
                            positions.push_back(vp1);
                            positions.push_back(vp2);
                        }
                    }

                    return positions;
                }

                //returns all positions, which match triangle_indices;
                inline geo::indexed_mesh::positions_t get_positions(const fbxsdk::FbxMesh* mesh, const std::vector<int32_t>& triangle_indices)
                {
                    auto points = mesh->GetControlPoints();
                    auto indices = mesh->GetPolygonVertices();

                    auto triangle_count = triangle_indices.size();
                    geo::indexed_mesh::positions_t positions;

                    auto node_transform = world_transform(mesh->GetNode());

                    for (auto triangle = 0; triangle < triangle_count; ++triangle)
                    {
                        auto triangle_to_fetch = triangle_indices[triangle];

                        auto i0 = indices[triangle_to_fetch * 3];
                        auto i1 = indices[triangle_to_fetch * 3 + 1];
                        auto i2 = indices[triangle_to_fetch * 3 + 2];

                        //positions
                        {
                            const double* v0 = points[i0];
                            const double* v1 = points[i1];
                            const double* v2 = points[i2];

                            math::float4  vr0 = math::set(static_cast<float>(v0[0]), static_cast<float>(v0[1]), static_cast<float>(v0[2]), 1.0f);
                            math::float4  vr1 = math::set(static_cast<float>(v1[0]), static_cast<float>(v1[1]), static_cast<float>(v1[2]), 1.0f);
                            math::float4  vr2 = math::set(static_cast<float>(v2[0]), static_cast<float>(v2[1]), static_cast<float>(v2[2]), 1.0f);

                            math::float4  vq0 = math::mul(vr0, node_transform);
                            math::float4  vq1 = math::mul(vr1, node_transform);
                            math::float4  vq2 = math::mul(vr2, node_transform);

                            geo::indexed_mesh::position_t vp0;
                            geo::indexed_mesh::position_t vp1;
                            geo::indexed_mesh::position_t vp2;

                            math::store3u(&vp0.x, vq0);
                            math::store3u(&vp1.x, vq1);
                            math::store3u(&vp2.x, vq2);

                            positions.push_back(vp0);
                            positions.push_back(vp1);
                            positions.push_back(vp2);
                        }
                    }

                    return positions;
                }

                inline geo::indexed_mesh::uvs_t get_uvs(const fbxsdk::FbxMesh* mesh)
                {
                    auto indices = mesh->GetPolygonVertices();
                    auto triangle_count = mesh->GetPolygonCount();
                    auto uv = mesh->GetElementUV(0);

                    geo::indexed_mesh::uvs_t        uvs;
                    get_uv_element*                 get_uv = nullptr;
                    get_uv_element_direct           get_uv_0(uv);
                    get_uv_element_index_to_direct  get_uv_1(uv);

                    if (uv->GetReferenceMode() == fbxsdk::FbxGeometryElement::eDirect)
                    {
                        get_uv = &get_uv_0;
                    }
                    else
                    {
                        assert(uv->GetReferenceMode() == fbxsdk::FbxGeometryElement::eIndexToDirect);
                        get_uv = &get_uv_1;
                    }

                    get_uv_point_index* get_point_index = nullptr;
                    get_uv_texture_index get_texture0(mesh);
                    get_uv_control_point_index get_texture1(indices);

                    if (uv->GetMappingMode() == fbxsdk::FbxGeometryElement::eByControlPoint)
                    {
                        get_point_index = &get_texture1;
                    }
                    else
                    {
                        assert(uv->GetMappingMode() == fbxsdk::FbxGeometryElement::eByPolygonVertex);
                        get_point_index = &get_texture0;
                        get_uv = &get_uv_0; //todo; check this, produces different uvs? the file data indicates otherwise
                    }

                    for (auto triangle = 0; triangle < triangle_count; ++triangle)
                    {
                        //uv
                        {
                            auto uvi0 = get_point_index->get_element(triangle, 0);
                            auto uvi1 = get_point_index->get_element(triangle, 1);
                            auto uvi2 = get_point_index->get_element(triangle, 2);

                            double* uv0 = get_uv->get_element(uvi0);
                            double* uv1 = get_uv->get_element(uvi1);
                            double* uv2 = get_uv->get_element(uvi2);

                            geo::indexed_mesh::uv_t uvp0 = { static_cast<float>(uv0[0]), static_cast<float>(uv0[1]) };
                            geo::indexed_mesh::uv_t uvp1 = { static_cast<float>(uv1[0]), static_cast<float>(uv1[1]) };
                            geo::indexed_mesh::uv_t uvp2 = { static_cast<float>(uv2[0]), static_cast<float>(uv2[1]) };

                            //transform to directx
                            uvp0.y = 1.0f - uvp0.y;
                            uvp1.y = 1.0f - uvp1.y;
                            uvp2.y = 1.0f - uvp2.y;

                            uvs.push_back(uvp0);
                            uvs.push_back(uvp1);
                            uvs.push_back(uvp2);
                        }
                    }
                    return uvs;
                }

                //returns all uvs, which match triangle_indices
                inline geo::indexed_mesh::uvs_t get_uvs(const fbxsdk::FbxMesh* mesh, const std::vector<int32_t>& triangle_indices)
                {
                    auto indices = mesh->GetPolygonVertices();
                    auto triangle_count = triangle_indices.size();
                    auto uv = mesh->GetElementUV(0);

                    geo::indexed_mesh::uvs_t        uvs;
                    get_uv_element*                 get_uv = nullptr;
                    get_uv_element_direct           get_uv_0(uv);
                    get_uv_element_index_to_direct  get_uv_1(uv);

                    if (uv->GetReferenceMode() == fbxsdk::FbxGeometryElement::eDirect)
                    {
                        get_uv = &get_uv_0;
                    }
                    else
                    {
                        assert(uv->GetReferenceMode() == fbxsdk::FbxGeometryElement::eIndexToDirect);
                        get_uv = &get_uv_1;
                    }

                    get_uv_point_index* get_point_index = nullptr;
                    get_uv_texture_index get_texture0(mesh);
                    get_uv_control_point_index get_texture1(indices);

                    if (uv->GetMappingMode() == fbxsdk::FbxGeometryElement::eByControlPoint)
                    {
                        get_point_index = &get_texture1;
                    }
                    else
                    {
                        assert(uv->GetMappingMode() == fbxsdk::FbxGeometryElement::eByPolygonVertex);
                        get_point_index = &get_texture0;
                        get_uv = &get_uv_0; //todo; check this, produces different uvs? the file data indicates otherwise
                    }

                    for (auto triangle = 0; triangle < triangle_count; ++triangle)
                    {
                        auto triangle_to_fetch = triangle_indices[triangle];
                        //uv
                        {
                            auto uvi0 = get_point_index->get_element(triangle_to_fetch, 0);
                            auto uvi1 = get_point_index->get_element(triangle_to_fetch, 1);
                            auto uvi2 = get_point_index->get_element(triangle_to_fetch, 2);

                            double* uv0 = get_uv->get_element(uvi0);
                            double* uv1 = get_uv->get_element(uvi1);
                            double* uv2 = get_uv->get_element(uvi2);

                            geo::indexed_mesh::uv_t uvp0 = { static_cast<float>(uv0[0]), static_cast<float>(uv0[1]) };
                            geo::indexed_mesh::uv_t uvp1 = { static_cast<float>(uv1[0]), static_cast<float>(uv1[1]) };
                            geo::indexed_mesh::uv_t uvp2 = { static_cast<float>(uv2[0]), static_cast<float>(uv2[1]) };

                            //transform to directx
                            uvp0.y = 1.0f - uvp0.y;
                            uvp1.y = 1.0f - uvp1.y;
                            uvp2.y = 1.0f - uvp2.y;

                            uvs.push_back(uvp0);
                            uvs.push_back(uvp1);
                            uvs.push_back(uvp2);
                        }
                    }
                    return uvs;
                }
            }
        }
    }
}

