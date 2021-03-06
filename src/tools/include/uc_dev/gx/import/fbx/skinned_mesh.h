#pragma once

#include <tuple>
#include <uc_dev/gx/import/geo/skinned_mesh.h>
#include <uc_dev/gx/import/fbx/fbx_common.h>
#include <uc_dev/gx/import/fbx/fbx_common_multi_material_mesh.h>

#include <uc_dev/gx/import/fbx/fbx_helpers.h>
#include <uc_dev/gx/import/fbx/fbx_transform_helper.h>
#include <uc_dev/gx/import/fbx/fbx_common_transform.h>
#include <uc_dev/gx/import/fbx/fbx_common_transform_dcc.h>
#include <map>

namespace uc
{
    namespace gx
    {
        namespace import
        {
            namespace fbx
            {
                struct joint_influence
                {
                    std::vector<float>    m_weight;
                    std::vector<uint32_t> m_index;
                };

                //////////////////////
                inline geo::skinned_mesh::blend_weight_t get_blend_weight(const std::vector<float>& v)
                {
                    geo::skinned_mesh::blend_weight_t r = {};
                    float* as_float = &r.x;
                    auto j = 0U;

                    for (auto&& i : v)
                    {
                        if (j >= 4)
                        {
                            break;
                        }
                        as_float[j++] = i;
                    }

                    return r;
                }

                //////////////////////
                inline geo::skinned_mesh::blend_index_t get_blend_index(const std::vector<uint32_t>& v)
                {
                    geo::skinned_mesh::blend_index_t r = {};
                    uint16_t* as_float = &r.x;
                    auto j = 0U;

                    for (auto&& i : v)
                    {
                        if (j >= 4)
                        {
                            break;
                        }
                        as_float[j++] = static_cast<uint16_t>(i);
                    }

                    return r;
                }

                inline geo::joint_transform joint_transform(const fbxsdk::FbxAMatrix& m)
                {
                    geo::joint_transform r;

                    r.m_rotation    = to_float4(m.GetQ());
                    r.m_translation = to_float4(m.GetT());

                    return r;
                }
                inline geo::joint_transform_matrix joint_transform_matrix(const fbxsdk::FbxAMatrix& m)
                {
                    geo::joint_transform_matrix r;

                    //move to row major
                    r.m_transform = to_float4x4(m);
                    return r;
                }

                inline std::vector<fbxsdk::FbxNode*> get_skeleton_nodes(const fbxsdk::FbxNode* n)
                {
                    std::vector<fbxsdk::FbxNode*> r;

                    transform_node_recursive(n, [&r](const fbxsdk::FbxNode* n)
                    {
                        if (n->GetNodeAttribute() && n->GetNodeAttribute()->GetAttributeType() == fbxsdk::FbxNodeAttribute::eSkeleton)
                        {
                            r.push_back(const_cast<fbxsdk::FbxNode*>(n));
                        }
                    });

                    return r;
                }

                inline std::map<fbxsdk::FbxNode*, uint16_t> get_joint_indices(const fbxsdk::FbxNode* root)
                {
                    auto skeletal_nodes = get_skeleton_nodes(root);

                    std::map<fbxsdk::FbxNode*, uint16_t> joint2index;

                    {
                        for (auto&& i = 0U; i < skeletal_nodes.size(); ++i)
                        {
                            auto n = skeletal_nodes[i];
                            joint2index.insert(std::make_pair(n, static_cast<uint16_t>(i)));
                        }
                    }
                    return joint2index;
                }

                inline std::vector<fbxsdk::FbxCluster*> get_joints_skinned(const fbxsdk::FbxMesh* mesh)
                {
                    std::vector<fbxsdk::FbxCluster*> joints;

                    int skinCount = mesh->GetDeformerCount(fbxsdk::FbxDeformer::eSkin);

                    for (int skinIndex = 0; skinIndex < skinCount; skinIndex++)
                    {
                        fbxsdk::FbxSkin* skin = (fbxsdk::FbxSkin *)mesh->GetDeformer(skinIndex, fbxsdk::FbxDeformer::eSkin);

                        int jointsCount = skin->GetClusterCount();

                        joints.resize(jointsCount);

                        for (int jointIndex = 0; jointIndex < jointsCount; jointIndex++)
                        {
                            fbxsdk::FbxCluster* joint = skin->GetCluster(jointIndex);
                            joints[jointIndex] = joint;
                        }
                    }

                    return joints;
                }

                inline std::map<fbxsdk::FbxNode*, fbxsdk::FbxCluster*> get_cluster_nodes(std::vector<fbxsdk::FbxCluster*> clusters)
                {
                    std::map<fbxsdk::FbxNode*, fbxsdk::FbxCluster*> r;

                    for (auto&& i : clusters)
                    {
                        r.insert(std::make_pair(i->GetLink(), i));
                    }

                    return r;
                }

                inline geo::skeleton_pose get_skeleton_pose(const fbxsdk::FbxMesh* mesh)
                {
                    std::vector<uint16_t>                parents;
                    std::map<fbxsdk::FbxNode*, uint16_t> joint2index;

                    geo::skeleton_pose skeleton;

                    auto evaluator              = mesh->GetScene()->GetAnimationEvaluator();
                    auto skeletal_nodes         = get_skeleton_nodes(mesh->GetScene()->GetRootNode());

                    std::vector<fbxsdk::FbxAMatrix> links;
                    {
                        skeleton.m_joint_local_pose.resize(skeletal_nodes.size());
                        for (auto&& i = 0U; i < skeletal_nodes.size(); ++i)
                        {
                            auto n = skeletal_nodes[i];
                            fbxsdk::FbxAMatrix t3 = evaluator->GetNodeGlobalTransform(n);
                            links.push_back(t3);

                            joint2index.insert(std::make_pair(n, static_cast<uint16_t>(i)));
                        }
                    }

                    parents.resize(skeletal_nodes.size());

                    //build up joint hierarchy, root node has parent 0xffff
                    for (auto i =0U; i <skeletal_nodes.size(); ++i)
                    {
                        const auto&& parent_index = joint2index.find(skeletal_nodes[i]->GetParent());
                        if (parent_index != joint2index.end())
                        {
                            parents[i] = parent_index->second;
                        }
                        else
                        {
                            parents[i] = 0xFFFF;
                        }
                    }

                    skeleton.m_joint_local_pose.resize(skeletal_nodes.size());

                    //transform global matrices to parent relative
                    for (auto i = 0U; i < skeletal_nodes.size(); ++i)
                    {
                        fbxsdk::FbxAMatrix parent;
                        fbxsdk::FbxAMatrix this_;
                        parent.SetIdentity();

                        if (parents[i] != 0xffff)
                        {
                            parent = links[parents[i]];
                        }

                        this_ = links[i];
                        auto t4 = parent.Inverse() * this_;
                        auto t5 = t4;
                        skeleton.m_joint_local_pose[i].m_transform = joint_transform(t5);
                        skeleton.m_joint_local_pose[i].m_transform_matrix = joint_transform_matrix(t5);
                    }

                    skeleton.m_skeleton.m_joints.resize(skeletal_nodes.size());

                    auto clusters       = get_joints_skinned(mesh);
                    auto skinned_joints = get_cluster_nodes(clusters);
                    auto geometry       = get_geometry(mesh->GetNode());

                    //fill up the inverse bind pose
                    for (auto i = 0U; i < skeletal_nodes.size(); ++i)
                    {
                        fbxsdk::FbxAMatrix parent;

                        fbxsdk::FbxAMatrix this_;
                        this_.SetIdentity();

                        skeleton.m_skeleton.m_joints[i].m_name = skeletal_nodes[i]->GetName();
                        skeleton.m_skeleton.m_joints[i].m_parent_index = parents[i];

                        auto skinned_joint = skinned_joints.find(skeletal_nodes[i]);

                        //if the joint participates in the skinning
                        if (skinned_joint != skinned_joints.end())
                        {
                            fbxsdk::FbxAMatrix init_bind_pose_joint;
                            skinned_joint->second->GetTransformLinkMatrix(init_bind_pose_joint);               // The transformation of the cluster(joint) at binding time from joint space to world space
                            fbxsdk::FbxAMatrix init_reference_global_position;
                            skinned_joint->second->GetTransformMatrix(init_reference_global_position);          // The transformation of the mesh at binding time

                            fbxsdk::FbxAMatrix mat_bind_pose_init = init_reference_global_position.Inverse() * init_bind_pose_joint * geometry;

                            auto t5 = mat_bind_pose_init;

                            skeleton.m_skeleton.m_joints[i].m_inverse_bind_pose = joint_transform(t5.Inverse());
                            skeleton.m_skeleton.m_joints[i].m_inverse_bind_pose2 = joint_transform_matrix(t5.Inverse());
                        }
                        else
                        {
                            auto t5 = this_;
                            skeleton.m_skeleton.m_joints[i].m_inverse_bind_pose = joint_transform(t5);
                            skeleton.m_skeleton.m_joints[i].m_inverse_bind_pose2 = joint_transform_matrix(t5);
                        }
                    }

                    return skeleton;
                }

                //////////////////////
                inline geo::skinned_mesh::blend_weights_t get_blend_weights(const fbxsdk::FbxMesh* mesh, const std::vector<int32_t>& triangle_indices)
                {
                    std::vector<joint_influence> influences;
                    influences.resize(mesh->GetControlPointsCount());

                    auto joint_indices = get_joint_indices(mesh->GetScene()->GetRootNode());

                    int skinCount = mesh->GetDeformerCount(fbxsdk::FbxDeformer::eSkin);
                    for (int skinIndex = 0; skinIndex < skinCount; skinIndex++)
                    {
                        FbxSkin* skin = (FbxSkin *)mesh->GetDeformer(skinIndex, fbxsdk::FbxDeformer::eSkin);

                        int jointsCount = skin->GetClusterCount();
                        for (int jointIndex = 0; jointIndex < jointsCount; jointIndex++)
                        {
                            fbxsdk::FbxCluster* joint = skin->GetCluster(jointIndex);

                            int influencedCount = joint->GetControlPointIndicesCount();

                            int* influenceIndices = joint->GetControlPointIndices();
                            double* influenceWeights = joint->GetControlPointWeights();

                            for (int influenceIndex = 0; influenceIndex < influencedCount; influenceIndex++)
                            {
                                int controlPointIndex = influenceIndices[influenceIndex];
                                assert(controlPointIndex < (int)influences.size());//"Invalid skin control point index"
                                influences[controlPointIndex].m_index.push_back(joint_indices.find(joint->GetLink())->second);
                                influences[controlPointIndex].m_weight.push_back((float)influenceWeights[influenceIndex]);
                            }
                        }
                    }

                    auto indices = mesh->GetPolygonVertices();
                    geo::skinned_mesh::blend_weights_t blend_weights;
                    for (auto triangle = 0; triangle < triangle_indices.size(); ++triangle)
                    {
                        auto triange_to_fetch = triangle_indices[triangle];
                        auto i0 = indices[triange_to_fetch * 3];
                        auto i1 = indices[triange_to_fetch * 3 + 1];
                        auto i2 = indices[triange_to_fetch * 3 + 2];

                        auto w0 = influences[i0];
                        auto w1 = influences[i1];
                        auto w2 = influences[i2];

                        auto wp0 = get_blend_weight(w0.m_weight);
                        auto wp1 = get_blend_weight(w1.m_weight);
                        auto wp2 = get_blend_weight(w2.m_weight);

                        blend_weights.push_back(wp0);
                        blend_weights.push_back(wp1);
                        blend_weights.push_back(wp2);
                    }

                    return blend_weights;
                }
                //////////////////////
                inline geo::skinned_mesh::blend_indices_t get_blend_indices(const fbxsdk::FbxMesh* mesh, const std::vector<int32_t>& triangle_indices)
                {
                    std::vector<joint_influence> influences;
                    influences.resize(mesh->GetControlPointsCount());

                    auto joint_indices = get_joint_indices(mesh->GetScene()->GetRootNode());

                    int skinCount = mesh->GetDeformerCount(fbxsdk::FbxDeformer::eSkin);
                    for (int skinIndex = 0; skinIndex < skinCount; skinIndex++)
                    {
                        FbxSkin* skin = (FbxSkin *)mesh->GetDeformer(skinIndex, fbxsdk::FbxDeformer::eSkin);
                        int jointsCount = skin->GetClusterCount();
                        for (int jointIndex = 0; jointIndex < jointsCount; jointIndex++)
                        {
                            fbxsdk::FbxCluster* joint = skin->GetCluster(jointIndex);

                            int influencedCount = joint->GetControlPointIndicesCount();

                            int* influenceIndices = joint->GetControlPointIndices();
                            double* influenceWeights = joint->GetControlPointWeights();

                            for (int influenceIndex = 0; influenceIndex < influencedCount; influenceIndex++)
                            {
                                int controlPointIndex = influenceIndices[influenceIndex];
                                assert(controlPointIndex < (int)influences.size());//"Invalid skin control point index"
                                influences[controlPointIndex].m_index.push_back(joint_indices.find(joint->GetLink())->second);
                                influences[controlPointIndex].m_weight.push_back((float)influenceWeights[influenceIndex]);
                            }
                        }
                    }

                    auto indices = mesh->GetPolygonVertices();
                    geo::skinned_mesh::blend_indices_t blend_indices;
                    for (auto triangle = 0; triangle < triangle_indices.size(); ++triangle)
                    {
                        auto triange_to_fetch = triangle_indices[triangle];
                        auto i0 = indices[triange_to_fetch * 3];
                        auto i1 = indices[triange_to_fetch * 3 + 1];
                        auto i2 = indices[triange_to_fetch * 3 + 2];

                        auto w0 = influences[i0];
                        auto w1 = influences[i1];
                        auto w2 = influences[i2];

                        auto ip0 = get_blend_index(w0.m_index);
                        auto ip1 = get_blend_index(w1.m_index);
                        auto ip2 = get_blend_index(w2.m_index);

                        blend_indices.push_back(ip0);
                        blend_indices.push_back(ip1);
                        blend_indices.push_back(ip2);
                    }

                    return blend_indices;
                }

                inline bool is_skinned_mesh(const fbxsdk::FbxMesh* mesh)
                {
                    return mesh->GetDeformerCount(fbxsdk::FbxDeformer::eSkin) > 0;
                }

                //returns which materials, which polygons affect
                inline std::vector< std::vector<int32_t> > get_material_indices(const fbxsdk::FbxMesh* mesh)
                {
                    auto material_count = mesh->GetElementMaterialCount();

                    if (material_count == 0)
                    {
                        ::uc::gx::throw_exception<uc::gx::fbx_exception>("mesh does not have assigned materials");
                    }

                    std::vector< std::vector<int32_t> > materials_indices;
                    //assign polygons to materials
                    for (auto i = 0; i < material_count; ++i)
                    {
                        auto&& element_material = mesh->GetElementMaterial(i);
                        auto&& material_indices = element_material->GetIndexArray();
                        auto count = material_indices.GetCount();

                        if (element_material->GetMappingMode() == fbxsdk::FbxLayerElement::eAllSame)
                        {
                            auto polygon_count = mesh->GetPolygonCount();
                            auto material_index_for_polygon = material_indices.GetAt(0);

                            if (material_index_for_polygon + 1 > materials_indices.size())
                            {
                                materials_indices.resize(material_index_for_polygon + 1);
                            }

                            materials_indices[material_index_for_polygon].reserve(polygon_count);
                            for (auto k = 0; k < polygon_count; ++k)
                            {
                                materials_indices[material_index_for_polygon].push_back(k);
                            }
                        }
                        else
                        {
                            for (auto j = 0; j < count; ++j)
                            {
                                auto material_index_for_polygon = material_indices.GetAt(j);
                                if (material_index_for_polygon + 1 > materials_indices.size())
                                {
                                    materials_indices.resize(material_index_for_polygon + 1);
                                }

                                materials_indices[material_index_for_polygon].push_back(j);
                            }
                        }
                    }

                    return materials_indices;
                }

                
                //////////////////////
                inline  std::vector<geo::skinned_mesh::positions_t> transform_dcc_positions(const std::vector<geo::skinned_mesh::positions_t>& positions, const fbx_context* ctx)
                {
                    std::vector<geo::skinned_mesh::positions_t> r;
                    ctx;

                    auto m0 = negate_z();
                    
                    r.reserve(positions.size());

                    for (auto&& p : positions)
                    {
                        std::vector<geo::skinned_mesh::position_t > pos;
                        pos.reserve(p.size());

                        for(auto&& p0: p)
                        {
                            auto v = transform_from_dcc(math::load3_point(&p0), ctx);
                            geo::skinned_mesh::position_t s;
                            math::store3u_point(&s, v);
                            pos.push_back(s);
                        }

                        r.push_back(pos);
                    }

                    return r;
                }

                inline geo::skinned_mesh::skeleton_pose_t transform_dcc_pose(const geo::skinned_mesh::skeleton_pose_t& p, const fbx_context* ctx)
                {
                    geo::skinned_mesh::skeleton_pose_t r = p;

                    for (auto && r : r.m_joint_local_pose)
                    {
                        auto m                              = r.m_transform_matrix.m_transform;
                        auto m0                             = transform_from_dcc(m, ctx);
                        auto test = r.m_transform.m_rotation;
                        r.m_transform_matrix.m_transform    = m0;
                        r.m_transform.m_rotation            = math::quaternion_normalize(math::matrix_2_quaternion_simd(math::rotation(m0)));
                        r.m_transform.m_translation         = math::translation(m0);

                    }

                    for (auto && r  : r.m_skeleton.m_joints)
                    {
                        auto m          = r.m_inverse_bind_pose2.m_transform;
                        auto m0         = transform_from_dcc(m, ctx);

                        r.m_inverse_bind_pose2.m_transform = m0;
                        r.m_inverse_bind_pose.m_rotation = math::quaternion_normalize(math::matrix_2_quaternion_simd(math::rotation(m0)));
                        r.m_inverse_bind_pose.m_translation = math::translation(m0);
                    }

                    return r;
                }
                

                //////////////////////
                inline std::shared_ptr<geo::skinned_mesh> create_skinned_mesh_internal(const fbxsdk::FbxMesh* mesh, const fbx_context* context)
                {
                    context;
                    const fbxsdk::FbxNode* mesh_node = mesh->GetNode();

                    //check
                    if (mesh == nullptr || !mesh->IsTriangleMesh())
                    {
                        ::uc::gx::throw_exception<uc::gx::fbx_exception>("file does not contain a triangle mesh");
                    }

                    assert(mesh_node);
                    assert(mesh->GetPolygonSize(0));

                    auto materials_indices = get_material_indices(mesh);
                    std::vector<geo::skinned_mesh::positions_t> positions;                  //positions used by every material
                    std::vector<geo::skinned_mesh::uvs_t>       uvs;                        //uvs used by every material
                    std::vector<geo::skinned_mesh::blend_weights_t>   blend_weights;        //blend_weights used by every material
                    std::vector<geo::skinned_mesh::blend_indices_t>   blend_indices;        //blend_indices used by every material

                    //get_positions
                    positions.resize(materials_indices.size());
                    uvs.resize(materials_indices.size());
                    blend_weights.resize(materials_indices.size());
                    blend_indices.resize(materials_indices.size());

                    for (auto i = 0U; i < materials_indices.size(); ++i)
                    {
                        positions[i] = get_positions(mesh, materials_indices[i]);
                        uvs[i] = get_uvs(mesh, materials_indices[i]);
                        blend_weights[i] = get_blend_weights(mesh, materials_indices[i]);
                        blend_indices[i] = get_blend_indices(mesh, materials_indices[i]);
                    }

                    //reindex faces, these are indices in the separated positions and uvs
                    std::vector<geo::skinned_mesh::faces_t>  faces; //uvs used by every material
                    faces.resize(materials_indices.size());

                    auto p = triangle_permuation(context);

                    for (auto i = 0; i < faces.size(); ++i)
                    {
                        //reorient triangles ccw, since they come cw from fbx, if needed
                        for (auto j = 0; j < materials_indices[i].size(); ++j)
                        {
                            auto triangle = j;
                            geo::skinned_mesh::face_t face;
                            face.v0 = triangle * 3 + p[0];
                            face.v1 = triangle * 3 + p[1];
                            face.v2 = triangle * 3 + p[2];

                            faces[i].push_back(face);
                        }
                    }

                    geo::skinned_mesh::skeleton_pose_t pose = get_skeleton_pose(mesh);

                    return std::make_shared<geo::skinned_mesh>(

                        transform_dcc_positions(positions, context),
                        std::move(uvs),
                        std::move(faces),
                        get_materials(mesh_node, static_cast<uint32_t>(materials_indices.size())),
                        std::move(blend_weights),
                        std::move(blend_indices),
                        transform_dcc_pose(pose, context)
                        );
                }

                //////////////////////
                inline std::shared_ptr<geo::skinned_mesh> create_skinned_mesh(const std::string& file_name)
                {
                    auto context    = load_fbx_file(file_name);
                    auto scene      = context->m_scene.get();

                    std::vector<fbxsdk::FbxMesh*> meshes;
                    meshes = get_meshes(scene->GetRootNode(), meshes);

                    for (auto& m : meshes)
                    {
                        m->RemoveBadPolygons();
                        m->ComputeBBox();
                    }

                    std::vector<  std::shared_ptr<geo::skinned_mesh> > multimeshes;
                    for (auto& m : meshes)
                    {
                        //skip meshes without skin and import only the first one
                        if (is_skinned_mesh(m) && multimeshes.empty())
                        {
                            multimeshes.push_back(create_skinned_mesh_internal(m, context.get()));
                            break;
                        }
                    }

                    //merge all multimaterial meshes into one
                    std::vector< geo::skinned_mesh::positions_t > pos;
                    std::vector< geo::skinned_mesh::uvs_t >       uv;
                    std::vector< geo::skinned_mesh::faces_t >     faces;
                    std::vector< geo::skinned_mesh::material >    mat;
                    std::vector< geo::skinned_mesh::blend_weights_t >    blend_weights;
                    std::vector< geo::skinned_mesh::blend_indices_t >    blend_indices;

                    geo::skeleton_pose pose;

                    for (auto&& m : multimeshes)
                    {
                        pos.insert(pos.end(), m->m_positions.begin(), m->m_positions.end());
                        uv.insert(uv.end(), m->m_uv.begin(), m->m_uv.end());
                        faces.insert(faces.end(), m->m_faces.begin(), m->m_faces.end());
                        mat.insert(mat.end(), m->m_materials.begin(), m->m_materials.end());
                        blend_weights.insert(blend_weights.end(), m->m_blend_weights.begin(), m->m_blend_weights.end());
                        blend_indices.insert(blend_indices.end(), m->m_blend_indices.begin(), m->m_blend_indices.end());
                        pose = m->m_skeleton_pose;
                    }

                    return std::make_shared<geo::skinned_mesh>(
                        std::move(pos), 
                        std::move(uv),
                        std::move(faces),
                        std::move(mat),
                        std::move(blend_weights),
                        std::move(blend_indices),
                        std::move(pose));
                }
            }
        }
    }
}

