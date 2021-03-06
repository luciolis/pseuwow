// Copyright (C) 2002-2007 Nikolaus Gebhardt
// This file is part of the "Irrlicht Engine".
// For conditions of distribution and use, see copyright notice in irrlicht.h
#include <iostream>
#include "irrlicht/irrlicht.h"
#include "CM2Mesh.h"
#include "CBoneSceneNode.h"
namespace irr
{
namespace scene
{


//! constructor
CM2Mesh::CM2Mesh()
: SkinningBuffers(0), HasAnimation(0), PreparedForSkinning(0),
    AnimationFrames(0.f), LastAnimatedFrame(0.f),
    AnimateNormals(true), HardwareSkinning(0), InterpolationMode(EIM_LINEAR)
{
    #ifdef _DEBUG
    setDebugName("CM2Mesh");
    #endif

    SkinningBuffers=&LocalBuffers;
}


//! destructor
CM2Mesh::~CM2Mesh()
{
    for (u32 i=0; i<AllJoints.size(); ++i)
        delete AllJoints[i];

    for (u32 j=0; j<LocalBuffers.size(); ++j)
    {
        if (LocalBuffers[j])
            LocalBuffers[j]->drop();
    }
}


//! returns the amount of frames in milliseconds.
//! If the amount is 1, it is a static (=non animated) mesh.
u32 CM2Mesh::getFrameCount() const
{
    return core::floor32(AnimationFrames);
}


//! returns the animated mesh based on a detail level. 0 is the lowest, 255 the highest detail. Note, that some Meshes will ignore the detail level.
IMesh* CM2Mesh::getMesh(s32 frame, s32 detailLevel, s32 startFrameLoop, s32 endFrameLoop)
{
    if (frame==-1)
        return this;
    animateMesh((f32)frame, 1.0f);

    skinMesh();
    return this;
}


//--------------------------------------------------------------------------
//            Keyframe Animation
//--------------------------------------------------------------------------


//! Animates this mesh's joints based on frame input
//! blend: {0-old position, 1-New position}
void CM2Mesh::animateMesh(f32 frame, f32 blend)
{
    if ( !HasAnimation  || LastAnimatedFrame==frame)
        return;

    LastAnimatedFrame=frame;
    SkinnedLastFrame=false;

    if (blend<=0.f)
        return; //No need to animate

    for (u32 i=0; i<AllJoints.size(); ++i)
    {
        SJoint *joint = AllJoints[i];

        const core::vector3df oldPosition = joint->Animatedposition;
        const core::vector3df oldScale = joint->Animatedscale;
        const core::quaternion oldRotation = joint->Animatedrotation;

        core::vector3df position = oldPosition;
        core::vector3df scale = oldScale;
        core::quaternion rotation = oldRotation;
        getFrameData(frame, joint,
                position, joint->positionHint,
                scale, joint->scaleHint,
                rotation, joint->rotationHint);

        if (blend==1.0f)
        {
            //No blending needed
            joint->Animatedposition = position;
            joint->Animatedscale = scale;
            joint->Animatedrotation = rotation;
        }
        else
        {
            //Blend animation
            joint->Animatedposition = core::lerp(oldPosition, position, blend);
            joint->Animatedscale = core::lerp(oldScale, scale, blend);
            joint->Animatedrotation.slerp(oldRotation, rotation, blend);
        }

    }

        //----------------
        buildAllAnimatedMatrices();
        //-----------------
}


void CM2Mesh::buildAllAnimatedMatrices(SJoint *joint, SJoint *parentJoint)
{

    if (!joint)
    {
        for (u32 i=0; i<RootJoints.size(); ++i)
        {
            buildAllAnimatedMatrices(RootJoints[i], 0);
        }
        return;
    }
    else
    {

          //Could be faster:
        if (joint->UseAnimationFrom &&
            (joint->UseAnimationFrom->PositionKeys.size() ||
            joint->UseAnimationFrom->ScaleKeys.size() ||
            joint->UseAnimationFrom->RotationKeys.size() ))
        {
            joint->GlobalAnimatedMatrix= core::matrix4();
            joint->GlobalAnimatedMatrix.setTranslation(joint->LocalMatrix.getTranslation());

            core::matrix4 tm;
            tm.setTranslation(joint->Animatedposition);
            joint->GlobalAnimatedMatrix*=tm;

            joint->GlobalAnimatedMatrix*=joint->Animatedrotation.getMatrix();

            core::matrix4 ts;
            ts.setScale(joint->Animatedscale);
            joint->GlobalAnimatedMatrix*=ts;
        }
        else
        {
        //If this joint has no animation we care a lot! Set it to starting position. --shlainn
          joint->GlobalAnimatedMatrix=core::matrix4();
          joint->GlobalAnimatedMatrix.setTranslation(joint->LocalMatrix.getTranslation());
        }

        if(parentJoint)
          joint->GlobalAnimatedMatrix=parentJoint->GlobalAnimatedMatrix*joint->GlobalAnimatedMatrix;
        for (u32 j=0; j<joint->Children.size(); ++j)
        {
          buildAllAnimatedMatrices(joint->Children[j], joint);
        }
    }
}


void CM2Mesh::getFrameData(f32 frame, SJoint *joint,
                core::vector3df &position, s32 &positionHint,
                core::vector3df &scale, s32 &scaleHint,
                core::quaternion &rotation, s32 &rotationHint)
{
    s32 foundPositionIndex = -1;
    s32 foundScaleIndex = -1;
    s32 foundRotationIndex = -1;

    if (joint->UseAnimationFrom)
    {
        const core::array<SPositionKey> &PositionKeys=joint->UseAnimationFrom->PositionKeys;
        const core::array<SScaleKey> &ScaleKeys=joint->UseAnimationFrom->ScaleKeys;
        const core::array<SRotationKey> &RotationKeys=joint->UseAnimationFrom->RotationKeys;

        if (PositionKeys.size())
        {
            foundPositionIndex = -1;

            //Test the Hints...
            if (positionHint>=0 && (u32)positionHint < PositionKeys.size())
            {
                //check this hint
                if (positionHint>0 && PositionKeys[positionHint].frame>=frame && PositionKeys[positionHint-1].frame<frame )
                    foundPositionIndex=positionHint;
                else if (positionHint+1 < (s32)PositionKeys.size())
                {
                    //check the next index
                    if ( PositionKeys[positionHint+1].frame>=frame &&
                            PositionKeys[positionHint+0].frame<frame)
                    {
                        positionHint++;
                        foundPositionIndex=positionHint;
                    }
                }
            }

            //The hint test failed, do a full scan...
            if (foundPositionIndex==-1)
            {
                for (u32 i=0; i<PositionKeys.size(); ++i)
                {
                    if (PositionKeys[i].frame >= frame) //Keys should to be sorted by frame
                    {
                        foundPositionIndex=i;
                        positionHint=i;
                        break;
                    }
                }
            }

            //Do interpolation...
            if (foundPositionIndex!=-1)
            {
                if (InterpolationMode==EIM_CONSTANT || foundPositionIndex==0)
                {
                    position = PositionKeys[foundPositionIndex].position;
                }
                else if (InterpolationMode==EIM_LINEAR)
                {
                    const SPositionKey& KeyA = PositionKeys[foundPositionIndex];
                    const SPositionKey& KeyB = PositionKeys[foundPositionIndex-1];

                    const f32 fd1 = frame - KeyA.frame;
                    const f32 fd2 = KeyB.frame - frame;
                    position = ((KeyB.position-KeyA.position)/(fd1+fd2))*fd1 + KeyA.position;
                }
            }
        }

        //------------------------------------------------------------

        if (ScaleKeys.size())
        {
            foundScaleIndex = -1;

            //Test the Hints...
            if (scaleHint>=0 && (u32)scaleHint < ScaleKeys.size())
            {
                //check this hint
                if (scaleHint>0 && ScaleKeys[scaleHint].frame>=frame && ScaleKeys[scaleHint-1].frame<frame )
                    foundScaleIndex=scaleHint;
                else if (scaleHint+1 < (s32)ScaleKeys.size())
                {
                    //check the next index
                    if ( ScaleKeys[scaleHint+1].frame>=frame &&
                            ScaleKeys[scaleHint+0].frame<frame)
                    {
                        scaleHint++;
                        foundScaleIndex=scaleHint;
                    }
                }
            }


            //The hint test failed, do a full scan...
            if (foundScaleIndex==-1)
            {
                for (u32 i=0; i<ScaleKeys.size(); ++i)
                {
                    if (ScaleKeys[i].frame >= frame) //Keys should to be sorted by frame
                    {
                        foundScaleIndex=i;
                        scaleHint=i;
                        break;
                    }
                }
            }

            //Do interpolation...
            if (foundScaleIndex!=-1)
            {
                if (InterpolationMode==EIM_CONSTANT || foundScaleIndex==0)
                {
                    scale = ScaleKeys[foundScaleIndex].scale;
                }
                else if (InterpolationMode==EIM_LINEAR)
                {
                    const SScaleKey& KeyA = ScaleKeys[foundScaleIndex];
                    const SScaleKey& KeyB = ScaleKeys[foundScaleIndex-1];

                    const f32 fd1 = frame - KeyA.frame;
                    const f32 fd2 = KeyB.frame - frame;
                    scale = ((KeyB.scale-KeyA.scale)/(fd1+fd2))*fd1 + KeyA.scale;
                }
            }
        }

        //-------------------------------------------------------------

        if (RotationKeys.size())
        {
            foundRotationIndex = -1;

            //Test the Hints...
            if (rotationHint>=0 && (u32)rotationHint < RotationKeys.size())
            {
                //check this hint
                if (rotationHint>0 && RotationKeys[rotationHint].frame>=frame && RotationKeys[rotationHint-1].frame<frame )
                    foundRotationIndex=rotationHint;
                else if (rotationHint+1 < (s32)RotationKeys.size())
                {
                    //check the next index
                    if ( RotationKeys[rotationHint+1].frame>=frame &&
                            RotationKeys[rotationHint+0].frame<frame)
                    {
                        rotationHint++;
                        foundRotationIndex=rotationHint;
                    }
                }
            }


            //The hint test failed, do a full scan...
            if (foundRotationIndex==-1)
            {
                for (u32 i=0; i<RotationKeys.size(); ++i)
                {
                    if (RotationKeys[i].frame >= frame) //Keys should be sorted by frame
                    {
                        foundRotationIndex=i;
                        rotationHint=i;
                        break;
                    }
                }
            }

            //Do interpolation...
            if (foundRotationIndex!=-1)
            {
                if (InterpolationMode==EIM_CONSTANT || foundRotationIndex==0)
                {
                    rotation = RotationKeys[foundRotationIndex].rotation;
                }
                else if (InterpolationMode==EIM_LINEAR)
                {
                    const SRotationKey& KeyA = RotationKeys[foundRotationIndex];
                    const SRotationKey& KeyB = RotationKeys[foundRotationIndex-1];

                    const f32 fd1 = frame - KeyA.frame;
                    const f32 fd2 = KeyB.frame - frame;
                    const f32 t = fd1/(fd1+fd2);

                    /*
                    f32 t = 0;
                    if (KeyA.frame!=KeyB.frame)
                        t = (frame-KeyA.frame) / (KeyB.frame - KeyA.frame);
                    */

                    rotation.slerp(KeyA.rotation, KeyB.rotation, t);
                }
            }
        }
    }
}

//--------------------------------------------------------------------------
//                Software Skinning
//--------------------------------------------------------------------------

//! Preforms a software skin on this mesh based of joint positions
void CM2Mesh::skinMesh()
{
    if ( !HasAnimation || SkinnedLastFrame )
        return;

    SkinnedLastFrame=true;
    if (!HardwareSkinning)
    {
        //Software skin....
        u32 i;

        //rigid animation
        for (i=0; i<AllJoints.size(); ++i)
        {
            for (u32 j=0; j<AllJoints[i]->AttachedMeshes.size(); ++j)
            {
                SSkinMeshBuffer* Buffer=(*SkinningBuffers)[ AllJoints[i]->AttachedMeshes[j] ];
                Buffer->Transformation=AllJoints[i]->GlobalAnimatedMatrix;
            }
        }

        //clear skinning helper array
        for (i=0; i<Vertices_Moved.size(); ++i)
            for (u32 j=0; j<Vertices_Moved[i].size(); ++j)
                Vertices_Moved[i][j]=false;

        //skin starting with the root joints
        for (i=0; i<RootJoints.size(); ++i)
            SkinJoint(RootJoints[i], 0);

        for (i=0; i<SkinningBuffers->size(); ++i)
            (*SkinningBuffers)[i]->setDirty();

    }
    updateBoundingBox();
}


void CM2Mesh::SkinJoint(SJoint *joint, SJoint *parentJoint)
{
    if (joint->Weights.size())
    {
        //Find this joints pull on vertices...
        core::matrix4 jointVertexPull(core::matrix4::EM4CONST_NOTHING);
        jointVertexPull.setbyproduct(joint->GlobalAnimatedMatrix, joint->GlobalInversedMatrix);

        core::vector3df thisVertexMove, thisNormalMove;

        core::array<scene::SSkinMeshBuffer*> &buffersUsed=*SkinningBuffers;

        //Skin Vertices Positions and Normals...
        for (u32 i=0; i<joint->Weights.size(); ++i)
        {
            SWeight& weight = joint->Weights[i];

            // Pull this vertex...
            jointVertexPull.transformVect(thisVertexMove, weight.StaticPos);

            if (AnimateNormals)
                jointVertexPull.rotateVect(thisNormalMove, weight.StaticNormal);

            if (! (*(weight.Moved)) )
            {
                *(weight.Moved) = true;

                buffersUsed[weight.buffer_id]->getVertex(weight.vertex_id)->Pos = thisVertexMove * weight.strength;

                if (AnimateNormals)
                    buffersUsed[weight.buffer_id]->getVertex(weight.vertex_id)->Normal = thisNormalMove * weight.strength;
            }
            else
            {
                buffersUsed[weight.buffer_id]->getVertex(weight.vertex_id)->Pos += thisVertexMove * weight.strength;

                if (AnimateNormals)
                    buffersUsed[weight.buffer_id]->getVertex(weight.vertex_id)->Normal += thisNormalMove * weight.strength;
            }
            buffersUsed[weight.buffer_id]->boundingBoxNeedsRecalculated();
        }
    }

    //Skin all children
    for (u32 j=0; j<joint->Children.size(); ++j)
        SkinJoint(joint->Children[j], joint);
}


E_ANIMATED_MESH_TYPE CM2Mesh::getMeshType() const
{
    return EAMT_M2;
}


//! Gets joint count.
u32 CM2Mesh::getJointCount() const
{
    return AllJoints.size();
}


//! Gets the name of a joint.
const c8* CM2Mesh::getJointName(u32 number) const
{
    if (number >= AllJoints.size())
        return 0;
    return AllJoints[number]->Name.c_str();
}


//! Gets a joint number from its name
s32 CM2Mesh::getJointNumber(const c8* name) const
{
    for (u32 i=0; i<AllJoints.size(); ++i)
    {
        if (AllJoints[i]->Name == name)
            return i;
    }

    return -1;
}


//! returns amount of mesh buffers.
u32 CM2Mesh::getMeshBufferCount() const
{
    return LocalBuffers.size();
}


//! returns pointer to a mesh buffer
IMeshBuffer* CM2Mesh::getMeshBuffer(u32 nr) const
{
    if (nr < LocalBuffers.size())
        return LocalBuffers[nr];
    else
        return 0;
}


//! Returns pointer to a mesh buffer which fits a material
IMeshBuffer* CM2Mesh::getMeshBuffer(const video::SMaterial &material) const
{
    for (u32 i=0; i<LocalBuffers.size(); ++i)
    {
        if (LocalBuffers[i]->getMaterial() == material)
            return LocalBuffers[i];
    }
    return 0;
}


//! returns an axis aligned bounding box
const core::aabbox3d<f32>& CM2Mesh::getBoundingBox() const
{
    return BoundingBox;
}


//! set user axis aligned bounding box
void CM2Mesh::setBoundingBox( const core::aabbox3df& box)
{
    BoundingBox = box;
}


//! sets a flag of all contained materials to a new value
void CM2Mesh::setMaterialFlag(video::E_MATERIAL_FLAG flag, bool newvalue)
{
    for (u32 i=0; i<LocalBuffers.size(); ++i)
        LocalBuffers[i]->Material.setFlag(flag,newvalue);
}

//! set the hardware mapping hint, for driver
void CM2Mesh::setHardwareMappingHint(E_HARDWARE_MAPPING newMappingHint,
                E_BUFFER_TYPE buffer)
{
        for (u32 i=0; i<LocalBuffers.size(); ++i)
                LocalBuffers[i]->setHardwareMappingHint(newMappingHint, buffer);
}


//! flags the meshbuffer as changed, reloads hardware buffers
void CM2Mesh::setDirty(E_BUFFER_TYPE buffer)
{
        for (u32 i=0; i<LocalBuffers.size(); ++i)
                LocalBuffers[i]->setDirty(buffer);
}


//! uses animation from another mesh
bool CM2Mesh::useAnimationFrom(const ISkinnedMesh *mesh)
{
    bool unmatched=false;

    for(u32 i=0;i<AllJoints.size();++i)
    {
        SJoint *joint=AllJoints[i];
        joint->UseAnimationFrom=0;

        if (joint->Name=="")
            unmatched=true;
        else
        {
            for(u32 j=0;j<mesh->getAllJoints().size();++j)
            {
                SJoint *otherJoint=mesh->getAllJoints()[j];
                if (joint->Name==otherJoint->Name)
                {
                    joint->UseAnimationFrom=otherJoint;
                }
            }
            if (!joint->UseAnimationFrom)
                unmatched=true;
        }
    }

    checkForAnimation();

    return !unmatched;
}


//!Update Normals when Animating
//!False= Don't animate them, faster
//!True= Update normals (default)
void CM2Mesh::updateNormalsWhenAnimating(bool on)
{
    AnimateNormals = on;
}


//!Sets Interpolation Mode
void CM2Mesh::setInterpolationMode(E_INTERPOLATION_MODE mode)
{
    InterpolationMode = mode;
}


core::array<scene::SSkinMeshBuffer*> &CM2Mesh::getMeshBuffers()
{
    return LocalBuffers;
}


core::array<CM2Mesh::SJoint*> &CM2Mesh::getAllJoints()
{
    return AllJoints;
}


const core::array<CM2Mesh::SJoint*> &CM2Mesh::getAllJoints() const
{
    return AllJoints;
}


//! (This feature is not implementated in irrlicht yet)
bool CM2Mesh::setHardwareSkinning(bool on)
{
    if (HardwareSkinning!=on)
    {

        if (on)
        {

            //set mesh to static pose...
            for (u32 i=0; i<AllJoints.size(); ++i)
            {
                SJoint *joint=AllJoints[i];
                for (u32 j=0; j<joint->Weights.size(); ++j)
                {
                    const u16 buffer_id=joint->Weights[j].buffer_id;
                    const u32 vertex_id=joint->Weights[j].vertex_id;
                    LocalBuffers[buffer_id]->getVertex(vertex_id)->Pos = joint->Weights[j].StaticPos;
                    LocalBuffers[buffer_id]->getVertex(vertex_id)->Normal = joint->Weights[j].StaticNormal;
                }
            }


        }

        HardwareSkinning=on;
    }
    return HardwareSkinning;
}


void CM2Mesh::CalculateGlobalMatrices(SJoint *joint,SJoint *parentJoint)
{
    if (!joint && parentJoint) // bit of protection from endless loops
        return;

    //Go through the root bones
    if (!joint)
    {
        for (u32 i=0; i<RootJoints.size(); ++i)
            CalculateGlobalMatrices(RootJoints[i],0);
        return;
    }

    if (!parentJoint)
        joint->LocalMatrix = joint->GlobalMatrix;
    else
        joint->LocalMatrix = joint->GlobalMatrix * parentJoint->GlobalInversedMatrix;
    joint->Animatedposition=core::vector3df(0,0,0);
    joint->LocalAnimatedMatrix=joint->LocalMatrix;
    joint->GlobalAnimatedMatrix=joint->GlobalMatrix;

    if (joint->GlobalInversedMatrix.isIdentity())//might be pre calculated
    {
        joint->GlobalInversedMatrix = joint->GlobalMatrix;
        joint->GlobalInversedMatrix.makeInverse(); // slow
    }

    for (u32 j=0; j<joint->Children.size(); ++j)
        CalculateGlobalMatrices(joint->Children[j],joint);
}


void CM2Mesh::checkForAnimation()
{
    u32 i,j;
    //Check for animation...
    HasAnimation = false;
    for(i=0;i<AllJoints.size();++i)
    {
        if (AllJoints[i]->UseAnimationFrom)
        {
            if (AllJoints[i]->UseAnimationFrom->PositionKeys.size() ||
                AllJoints[i]->UseAnimationFrom->ScaleKeys.size() ||
                AllJoints[i]->UseAnimationFrom->RotationKeys.size() )
            {
                HasAnimation = true;
            }
        }
    }

    //meshes with weights, are still counted as animated for ragdolls, etc
    if (!HasAnimation)
    {
        for(i=0;i<AllJoints.size();++i)
        {
            if (AllJoints[i]->Weights.size())
                HasAnimation = true;
        }
    }

    if (HasAnimation)
    {
        //--- Find the length of the animation ---
        AnimationFrames=0;
        for(i=0;i<AllJoints.size();++i)
        {
            if (AllJoints[i]->UseAnimationFrom)
            {
                if (AllJoints[i]->UseAnimationFrom->PositionKeys.size())
                    if (AllJoints[i]->UseAnimationFrom->PositionKeys.getLast().frame > AnimationFrames)
                        AnimationFrames=AllJoints[i]->UseAnimationFrom->PositionKeys.getLast().frame;

                if (AllJoints[i]->UseAnimationFrom->ScaleKeys.size())
                    if (AllJoints[i]->UseAnimationFrom->ScaleKeys.getLast().frame > AnimationFrames)
                        AnimationFrames=AllJoints[i]->UseAnimationFrom->ScaleKeys.getLast().frame;

                if (AllJoints[i]->UseAnimationFrom->RotationKeys.size())
                    if (AllJoints[i]->UseAnimationFrom->RotationKeys.getLast().frame > AnimationFrames)
                        AnimationFrames=AllJoints[i]->UseAnimationFrom->RotationKeys.getLast().frame;
            }
        }
    }

    if (HasAnimation && !PreparedForSkinning)
    {
        PreparedForSkinning=true;

        //check for bugs:
        for(i=0; i < AllJoints.size(); ++i)
        {
            SJoint *joint = AllJoints[i];
            for (j=0; j<joint->Weights.size(); ++j)
            {
                const u16 buffer_id=joint->Weights[j].buffer_id;
                const u32 vertex_id=joint->Weights[j].vertex_id;
                //check for invalid ids
                if (buffer_id>=LocalBuffers.size())
                {
                    //printf("Skinned Mesh: Weight buffer id too large");
                    joint->Weights[j].buffer_id = joint->Weights[j].vertex_id =0;
                }
                else if (vertex_id>=LocalBuffers[buffer_id]->getVertexCount())
                {
                    //printf("Skinned Mesh: Weight vertex id too large");
                    joint->Weights[j].buffer_id = joint->Weights[j].vertex_id =0;
                }
            }
        }

        //An array used in skinning

        for (i=0; i<Vertices_Moved.size(); ++i)
            for (j=0; j<Vertices_Moved[i].size(); ++j)
                Vertices_Moved[i][j] = false;

        // For skinning: cache weight values for speed

        for (i=0; i<AllJoints.size(); ++i)
        {
            SJoint *joint = AllJoints[i];
            for (j=0; j<joint->Weights.size(); ++j)
            {
                const u16 buffer_id=joint->Weights[j].buffer_id;
                const u32 vertex_id=joint->Weights[j].vertex_id;

                joint->Weights[j].Moved = &Vertices_Moved[buffer_id] [vertex_id];
                joint->Weights[j].StaticPos = LocalBuffers[buffer_id]->getVertex(vertex_id)->Pos;
                joint->Weights[j].StaticNormal = LocalBuffers[buffer_id]->getVertex(vertex_id)->Normal;

            }
        }

        // normalize weights
        normalizeWeights();
    }
}


//! called by loader after populating with mesh and bone data
void CM2Mesh::finalize()
{
    u32 i;
    LastAnimatedFrame=-1;
    SkinnedLastFrame=false;

    //calculate bounding box

    for (i=0; i<LocalBuffers.size(); ++i)
    {
        LocalBuffers[i]->recalculateBoundingBox();
    }

    if (AllJoints.size() || RootJoints.size())
    {
        // populate AllJoints or RootJoints, depending on which is empty
        if (!RootJoints.size())
        {

            for(u32 CheckingIdx=0; CheckingIdx < AllJoints.size(); ++CheckingIdx)
            {

                bool foundParent=false;
                for(i=0; i < AllJoints.size(); ++i)
                {
                    for(u32 n=0; n < AllJoints[i]->Children.size(); ++n)
                    {
                        if (AllJoints[i]->Children[n] == AllJoints[CheckingIdx])
                            foundParent=true;
                    }
                }

                if (!foundParent)
                    RootJoints.push_back(AllJoints[CheckingIdx]);
            }
        }
        else
        {
            AllJoints=RootJoints;
        }
    }

    for(i=0; i < AllJoints.size(); ++i)
    {
        AllJoints[i]->UseAnimationFrom=AllJoints[i];
    }

    //Set array sizes...

    for (i=0; i<LocalBuffers.size(); ++i)
    {
        Vertices_Moved.push_back( core::array<bool>() );
        Vertices_Moved[i].set_used(LocalBuffers[i]->getVertexCount());
    }

    //Todo: optimise keys here...

    checkForAnimation();

if (HasAnimation)
    {
        //--- optimize and check keyframes ---
        for(i=0;i<AllJoints.size();++i)
        {
            core::array<SPositionKey> &PositionKeys =AllJoints[i]->PositionKeys;
            core::array<SScaleKey> &ScaleKeys = AllJoints[i]->ScaleKeys;
            core::array<SRotationKey> &RotationKeys = AllJoints[i]->RotationKeys;
            if (PositionKeys.size()>2)
            {
                for(u32 j=0;j<PositionKeys.size()-2;++j)
                {
                    if (PositionKeys[j].position == PositionKeys[j+1].position && PositionKeys[j+1].position == PositionKeys[j+2].position)
                    {
                        PositionKeys.erase(j+1); //the middle key is unneeded
                        --j;
                    }
                }
            }

            if (PositionKeys.size()>1)
            {
                for(u32 j=0;j<PositionKeys.size()-1;++j)
                {
                    if (PositionKeys[j].frame >= PositionKeys[j+1].frame) //bad frame, unneed and may cause problems
                    {
                        PositionKeys.erase(j+1);
                        --j;
                    }
                }
            }

            if (ScaleKeys.size()>2)
            {
                for(u32 j=0;j<ScaleKeys.size()-2;++j)
                {
                    if (ScaleKeys[j].scale == ScaleKeys[j+1].scale && ScaleKeys[j+1].scale == ScaleKeys[j+2].scale)
                    {
                        ScaleKeys.erase(j+1); //the middle key is unneeded
                        --j;
                    }
                }
            }

            if (ScaleKeys.size()>1)
            {
                for(u32 j=0;j<ScaleKeys.size()-1;++j)
                {
                    if (ScaleKeys[j].frame >= ScaleKeys[j+1].frame) //bad frame, unneed and may cause problems
                    {
                        ScaleKeys.erase(j+1);
                        --j;
                    }
                }
            }

            if (RotationKeys.size()>2)
            {
                for(u32 j=0;j<RotationKeys.size()-2;++j)
                {
                    if (RotationKeys[j].rotation == RotationKeys[j+1].rotation && RotationKeys[j+1].rotation == RotationKeys[j+2].rotation)
                    {
                        RotationKeys.erase(j+1); //the middle key is unneeded
                        --j;
                    }
                }
            }

            if (RotationKeys.size()>1)
            {
                for(u32 j=0;j<RotationKeys.size()-1;++j)
                {
                    if (RotationKeys[j].frame >= RotationKeys[j+1].frame) //bad frame, unneed and may cause problems
                    {
                        RotationKeys.erase(j+1);
                        --j;
                    }
                }
            }


            //Fill empty keyframe areas
            if (PositionKeys.size())
            {
                SPositionKey *Key;
                Key=&PositionKeys[0];//getFirst
                if (Key->frame!=0)
                {
                    PositionKeys.push_front(*Key);
                    Key=&PositionKeys[0];//getFirst
                    Key->frame=0;
                }

                Key=&PositionKeys.getLast();
                if (Key->frame!=AnimationFrames)
                {
                    PositionKeys.push_back(*Key);
                    Key=&PositionKeys.getLast();
                    Key->frame=AnimationFrames;
                }
            }

            if (ScaleKeys.size())
            {
                SScaleKey *Key;
                Key=&ScaleKeys[0];//getFirst
                if (Key->frame!=0)
                {
                    ScaleKeys.push_front(*Key);
                    Key=&ScaleKeys[0];//getFirst
                    Key->frame=0;
                }

                Key=&ScaleKeys.getLast();
                if (Key->frame!=AnimationFrames)
                {
                    ScaleKeys.push_back(*Key);
                    Key=&ScaleKeys.getLast();
                    Key->frame=AnimationFrames;
                }
            }

            if (RotationKeys.size())
            {
                SRotationKey *Key;
                Key=&RotationKeys[0];//getFirst
                if (Key->frame!=0)
                {
                    RotationKeys.push_front(*Key);
                    Key=&RotationKeys[0];//getFirst
                    Key->frame=0;
                }

                Key=&RotationKeys.getLast();
                if (Key->frame!=AnimationFrames)
                {
                    RotationKeys.push_back(*Key);
                    Key=&RotationKeys.getLast();
                    Key->frame=AnimationFrames;
                }
            }

        }
    }

    //Needed for animation and skinning...

    CalculateGlobalMatrices(0,0);

    //rigid animation for non animated meshes
    for (i=0; i<AllJoints.size(); ++i)
    {
        for (u32 j=0; j<AllJoints[i]->AttachedMeshes.size(); ++j)
        {
            SSkinMeshBuffer* Buffer=(*SkinningBuffers)[ AllJoints[i]->AttachedMeshes[j] ];
            Buffer->Transformation=AllJoints[i]->GlobalAnimatedMatrix;
        }
    }

    //calculate bounding box
    if (LocalBuffers.empty())
        BoundingBox.reset(0,0,0);
    else
    {
        irr::core::aabbox3df bb(LocalBuffers[0]->BoundingBox);
        LocalBuffers[0]->Transformation.transformBoxEx(bb);
        BoundingBox.reset(bb);

        for (u32 j=1; j<LocalBuffers.size(); ++j)
        {
            bb = LocalBuffers[j]->BoundingBox;
            LocalBuffers[j]->Transformation.transformBoxEx(bb);

            BoundingBox.addInternalBox(bb);
        }
    }
}

void CM2Mesh::updateBoundingBox(void)
{
    if(!SkinningBuffers)
        return;
    core::array<SSkinMeshBuffer*> & buffer = *SkinningBuffers;
    BoundingBox.reset(0,0,0);

    if (!buffer.empty())
    {
        for (u32 j=0; j<buffer.size(); ++j)
        {
            buffer[j]->recalculateBoundingBox();
            core::aabbox3df bb = buffer[j]->BoundingBox;
            buffer[j]->Transformation.transformBoxEx(bb);

            BoundingBox.addInternalBox(bb);
        }
    }
}



scene::SSkinMeshBuffer *CM2Mesh::addMeshBuffer()
{
    scene::SSkinMeshBuffer *buffer=new scene::SSkinMeshBuffer();
    LocalBuffers.push_back(buffer);
    return buffer;
}


CM2Mesh::SJoint *CM2Mesh::addJoint(SJoint *parent)
{
    SJoint *joint=new SJoint;

    AllJoints.push_back(joint);
    if (!parent)
    {
        //Add root joints to array in finalize()
    }
    else
    {
        //Set parent (Be careful of the mesh loader also setting the parent)
        parent->Children.push_back(joint);
    }

    return joint;
}


CM2Mesh::SPositionKey *CM2Mesh::addPositionKey(SJoint *joint)
{
    if (!joint)
        return 0;

    joint->PositionKeys.push_back(SPositionKey());
    return &joint->PositionKeys.getLast();
}


CM2Mesh::SScaleKey *CM2Mesh::addScaleKey(SJoint *joint)
{
    if (!joint)
        return 0;

    joint->ScaleKeys.push_back(SScaleKey());
    return &joint->ScaleKeys.getLast();
}


CM2Mesh::SRotationKey *CM2Mesh::addRotationKey(SJoint *joint)
{
    if (!joint)
        return 0;

    joint->RotationKeys.push_back(SRotationKey());
    return &joint->RotationKeys.getLast();
}


CM2Mesh::SWeight *CM2Mesh::addWeight(SJoint *joint)
{
    if (!joint)
        return 0;

    joint->Weights.push_back(SWeight());
    return &joint->Weights.getLast();
}


bool CM2Mesh::isStatic()
{
    return !HasAnimation;
}


void CM2Mesh::normalizeWeights()
{
    // note: unsure if weights ids are going to be used.

    // Normalise the weights on bones....

    u32 i,j;
    core::array< core::array<f32> > Vertices_TotalWeight;

    for (i=0; i<LocalBuffers.size(); ++i)
    {
        Vertices_TotalWeight.push_back(core::array<f32>());
        Vertices_TotalWeight[i].set_used(LocalBuffers[i]->getVertexCount());
    }

    for (i=0; i<Vertices_TotalWeight.size(); ++i)
        for (j=0; j<Vertices_TotalWeight[i].size(); ++j)
            Vertices_TotalWeight[i][j] = 0;

    for (i=0; i<AllJoints.size(); ++i)
    {
        SJoint *joint=AllJoints[i];
        for (j=0; j<joint->Weights.size(); ++j)
        {
            if (joint->Weights[j].strength<=0)//Check for invalid weights
            {
                joint->Weights.erase(j);
                --j;
            }
            else
            {
                Vertices_TotalWeight[ joint->Weights[j].buffer_id ] [ joint->Weights[j].vertex_id ] += joint->Weights[j].strength;
            }
        }
    }

    for (i=0; i<AllJoints.size(); ++i)
    {
        SJoint *joint=AllJoints[i];
        for (j=0; j< joint->Weights.size(); ++j)
        {
            const f32 total = Vertices_TotalWeight[ joint->Weights[j].buffer_id ] [ joint->Weights[j].vertex_id ];
            if (total != 0 && total != 1)
                joint->Weights[j].strength /= total;
        }
    }
}


void CM2Mesh::recoverJointsFromMesh(core::array<IBoneSceneNode*> &JointChildSceneNodes)
{
    for (u32 i=0;i<AllJoints.size();++i)
    {
        IBoneSceneNode* node=JointChildSceneNodes[i];
        SJoint *joint=AllJoints[i];
        node->setPosition( joint->LocalAnimatedMatrix.getTranslation() );
        node->setRotation( joint->LocalAnimatedMatrix.getRotationDegrees() );

        //node->setScale( joint->LocalAnimatedMatrix.getScale() );

        node->positionHint=joint->positionHint;
        node->scaleHint=joint->scaleHint;
        node->rotationHint=joint->rotationHint;

        //node->setAbsoluteTransformation(joint->GlobalMatrix); //not going to work

        //Note: This updateAbsolutePosition will not work well if joints are not nested like b3d
        //node->updateAbsolutePosition();
    }
}


void CM2Mesh::transferJointsToMesh(const core::array<IBoneSceneNode*> &JointChildSceneNodes)
{
    for (u32 i=0; i<AllJoints.size(); ++i)
    {
        const IBoneSceneNode* const node=JointChildSceneNodes[i];
        SJoint *joint=AllJoints[i];

        joint->LocalAnimatedMatrix.setTranslation(node->getPosition());
        joint->LocalAnimatedMatrix.setRotationDegrees(node->getRotation());

        //joint->LocalAnimatedMatrix.setScale( node->getScale() );

        joint->positionHint=node->positionHint;
        joint->scaleHint=node->scaleHint;
        joint->rotationHint=node->rotationHint;

        if (node->getSkinningSpace()==EBSS_GLOBAL)
            joint->GlobalSkinningSpace=true;
        else
            joint->GlobalSkinningSpace=false;
    }
    //Remove cache, temp...
    LastAnimatedFrame=-1;
    SkinnedLastFrame=false;
}


void CM2Mesh::transferOnlyJointsHintsToMesh(const core::array<IBoneSceneNode*> &JointChildSceneNodes)
{
    for (u32 i=0;i<AllJoints.size();++i)
    {
        const IBoneSceneNode* const node=JointChildSceneNodes[i];
        SJoint *joint=AllJoints[i];

        joint->positionHint=node->positionHint;
        joint->scaleHint=node->scaleHint;
        joint->rotationHint=node->rotationHint;
    }
}


void CM2Mesh::createJoints(core::array<IBoneSceneNode*> &JointChildSceneNodes,
        IAnimatedMeshSceneNode* AnimatedMeshSceneNode,
        ISceneManager* SceneManager)
{
    u32 i;

    //Create new joints
    for (i=0;i<AllJoints.size();++i)
    {
        JointChildSceneNodes.push_back(new CBoneSceneNode(0, SceneManager, 0, i, AllJoints[i]->Name.c_str()));
    }

    //Match up parents
    for (i=0;i<JointChildSceneNodes.size();++i)
    {
        IBoneSceneNode* node=JointChildSceneNodes[i];
        const SJoint* const joint=AllJoints[i]; //should be fine

        s32 parentID=-1;

        for (u32 j=0;j<AllJoints.size();++j)
        {
            if (i!=j && parentID==-1)
            {
                const SJoint* const parentTest=AllJoints[j];
                for (u32 n=0;n<parentTest->Children.size();++n)
                {
                    if (parentTest->Children[n]==joint)
                    {
                        parentID=j;
                        break;
                    }
                }
            }
        }

        if (parentID!=-1)
            node->setParent( JointChildSceneNodes[parentID] );
        else
            node->setParent( AnimatedMeshSceneNode );

        node->drop();
    }
}


void CM2Mesh::convertMeshToTangents()
{
    // now calculate tangents
    for (u32 b=0; b < LocalBuffers.size(); ++b)
    {
        if (LocalBuffers[b])
        {
            LocalBuffers[b]->convertToTangents();

            const s32 idxCnt = LocalBuffers[b]->getIndexCount();

            u16* idx = LocalBuffers[b]->getIndices();
            video::S3DVertexTangents* v =
                (video::S3DVertexTangents*)LocalBuffers[b]->getVertices();

            for (s32 i=0; i<idxCnt; i+=3)
            {
                calculateTangents(
                    v[idx[i+0]].Normal,
                    v[idx[i+0]].Tangent,
                    v[idx[i+0]].Binormal,
                    v[idx[i+0]].Pos,
                    v[idx[i+1]].Pos,
                    v[idx[i+2]].Pos,
                    v[idx[i+0]].TCoords,
                    v[idx[i+1]].TCoords,
                    v[idx[i+2]].TCoords);

                calculateTangents(
                    v[idx[i+1]].Normal,
                    v[idx[i+1]].Tangent,
                    v[idx[i+1]].Binormal,
                    v[idx[i+1]].Pos,
                    v[idx[i+2]].Pos,
                    v[idx[i+0]].Pos,
                    v[idx[i+1]].TCoords,
                    v[idx[i+2]].TCoords,
                    v[idx[i+0]].TCoords);

                calculateTangents(
                    v[idx[i+2]].Normal,
                    v[idx[i+2]].Tangent,
                    v[idx[i+2]].Binormal,
                    v[idx[i+2]].Pos,
                    v[idx[i+0]].Pos,
                    v[idx[i+1]].Pos,
                    v[idx[i+2]].TCoords,
                    v[idx[i+0]].TCoords,
                    v[idx[i+1]].TCoords);
            }
        }
    }
}


void CM2Mesh::calculateTangents(
    core::vector3df& normal,
    core::vector3df& tangent,
    core::vector3df& binormal,
    core::vector3df& vt1, core::vector3df& vt2, core::vector3df& vt3, // vertices
    core::vector2df& tc1, core::vector2df& tc2, core::vector2df& tc3) // texture coords
{
    core::vector3df v1 = vt1 - vt2;
    core::vector3df v2 = vt3 - vt1;
    normal = v2.crossProduct(v1);
    normal.normalize();

    // binormal

    f32 deltaX1 = tc1.X - tc2.X;
    f32 deltaX2 = tc3.X - tc1.X;
    binormal = (v1 * deltaX2) - (v2 * deltaX1);
    binormal.normalize();

    // tangent

    f32 deltaY1 = tc1.Y - tc2.Y;
    f32 deltaY2 = tc3.Y - tc1.Y;
    tangent = (v1 * deltaY2) - (v2 * deltaY1);
    tangent.normalize();

    // adjust

    core::vector3df txb = tangent.crossProduct(binormal);
    if (txb.dotProduct(normal) < 0.0f)
    {
        tangent *= -1.0f;
        binormal *= -1.0f;
    }
}


void CM2Mesh::getFrameLoop(u32 id, s32 &start, s32 &end)
{
  core::map<u32, core::array<u32> >::Node* n =AnimationLookup.find(id);
  if(n)
  {
    f32 r = (rand() % 32767)/32767.0f;
    u8 x = 0;
    bool found = false;
    M2Animation a;
    while(!found && x < n->getValue().size())
    {
      a = Animations[n->getValue()[x]];
      if(r>a.probability)
        x++;
      else
        found=true;
    }
    start = a.begin;
    end = a.end;
  }
  else
    return;

}

void CM2Mesh::newAnimation(u32 id, s32 start, s32 end, f32 probability)
{
  core::array<u32> temp;
  f32 prev_prob;
  if(AnimationLookup.find(id)==0)
  {
    prev_prob = 0.0f;
  }
  else
  {
    temp = AnimationLookup[id];
    prev_prob = Animations[temp[temp.size()-1]].probability;
  }
  temp.push_back(Animations.size());
  AnimationLookup[id] = temp;

  M2Animation a;
  a.begin = start;
  a.end = end;
  a.probability = probability + prev_prob;
  Animations.push_back(a);
}

// Switch Geosets on and off
void CM2Mesh::setGeoSetRender(u32 id, bool render)//this sets the render status for a geoset ID
{
  for(u16 i = 0; i < GeoSetID.size(); i++)
  {
    if(GeoSetID[i]==id)
    {
      GeoSetRender[i]=render;
    }
  }
};

//DEBUG FUNCTION: Switch submeshes on and off
void CM2Mesh::setMBRender(u32 id, bool render)//this sets the render status for a geoset ID
{
  if(GeoSetRender.size()>id)
  {
    GeoSetRender[id]=render;
  }
};



bool CM2Mesh::getGeoSetRender(u32 meshbufferNumber)//This gets the render status for a specific mesh buffer
{
  if(GeoSetRender.size()>meshbufferNumber)
  {
    return GeoSetRender[meshbufferNumber];
  }
  else
    return false;
};


} // end namespace scene
} // end namespace irr

