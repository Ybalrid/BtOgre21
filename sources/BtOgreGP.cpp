/*
 * =============================================================================================
 *
 *       Filename:  BtOgreGP.cpp
 *
 *    Description:  BtOgre "Graphics to Physics" implementation.
 *
 *        Version:  1.1
 *        Created:  27/12/2008 01:47:56 PM
 *
 *         Author:  Nikhilesh (nikki), Arthur Brainville (Ybalrid)
 *
 * =============================================================================================
 */

#include "BtOgrePG.h"
#include "BtOgreGP.h"
#include "BtOgreExtras.h"

using namespace Ogre;
using namespace BtOgre;

/*
 * =============================================================================================
 * BtOgre::VertexIndexToShape
 * =============================================================================================
 */

void log(const std::string& message)
{
	Ogre::LogManager::getSingleton().logMessage("BtOgreLog : " + message);
}

void VertexIndexToShape::appendVertexData(const v1::VertexData *vertex_data)
{
	if (!vertex_data)
		return;

	const auto previousSize = mVertexBuffer.size();

	//Resize to fit new data
	mVertexBuffer.resize(previousSize + vertex_data->vertexCount);

	// Get the positional buffer element
	const v1::VertexElement* posElem = vertex_data->vertexDeclaration->findElementBySemantic(VES_POSITION);
	v1::HardwareVertexBufferSharedPtr vbuf = vertex_data->vertexBufferBinding->getBuffer(posElem->getSource());

	const unsigned int vertexSize = static_cast<unsigned int>(vbuf->getVertexSize());

	//Get read only access to the row buffer
	unsigned char* vertex = static_cast<unsigned char*>(vbuf->lock(v1::HardwareBuffer::HBL_READ_ONLY));
	float* rawVertex; // this pointer will be used a a buffer to write the [float, float, float] array of the vertex

	//Write data to the vertex buffer
	const unsigned int vertexCount = static_cast<unsigned int>(vertex_data->vertexCount);
	for (unsigned int j = 0; j < vertexCount; ++j)
	{
		//Get pointer to the start of this vertex
		posElem->baseVertexPointerToElement(vertex, &rawVertex);
		vertex += vertexSize;

		mVertexBuffer[previousSize + j] = (mTransform * Vector3{ rawVertex });
	}

	//Release vertex buffer opened in read only
	vbuf->unlock();
}

void VertexIndexToShape::addAnimatedVertexData(const v1::VertexData *vertex_data,
	const v1::VertexData *blend_data,
	const v1::Mesh::IndexMap *indexMap)
{
	// Get the bone index element
	assert(vertex_data);

	const v1::VertexData *data = blend_data;

	// Get current size;
	const auto prev_size = mVertexBuffer.size();

	// Get the positional buffer element
	appendVertexData(data);

	const v1::VertexElement* bneElem = vertex_data->vertexDeclaration->findElementBySemantic(VES_BLEND_INDICES);
	assert(bneElem);

	v1::HardwareVertexBufferSharedPtr vbuf = vertex_data->vertexBufferBinding->getBuffer(bneElem->getSource());
	const unsigned int vSize = static_cast<unsigned int>(vbuf->getVertexSize());
	unsigned char* vertex = static_cast<unsigned char*>(vbuf->lock(v1::HardwareBuffer::HBL_READ_ONLY));

	unsigned char* pBone;

	if (!mBoneIndex)
		mBoneIndex = new BoneIndex();
	BoneIndex::iterator i;

	///Todo : get rid of that
	Vector3 * curVertices = &mVertexBuffer.data()[prev_size];

	const unsigned int vertexCount = static_cast<unsigned int>(vertex_data->vertexCount);
	for (unsigned int j = 0; j < vertexCount; ++j)
	{
		bneElem->baseVertexPointerToElement(vertex, &pBone);
		vertex += vSize;

		const unsigned char currBone = (indexMap) ? (*indexMap)[*pBone] : *pBone;
		i = mBoneIndex->find(currBone);
		Vector3Array* l = nullptr;
		if (i == mBoneIndex->end())
		{
			l = new Vector3Array;
			mBoneIndex->insert(BoneKeyIndex(currBone, l));
		}
		else
		{
			l = i->second;
		}

		l->push_back(*curVertices);

		curVertices++;
	}
	vbuf->unlock();
}

void VertexIndexToShape::appendIndexData(v1::IndexData *data, const unsigned int offset)
{
	const auto appendedIndexes = data->indexCount;
	const auto previousSize = mIndexBuffer.size();

	mIndexBuffer.resize(previousSize + appendedIndexes);

	v1::HardwareIndexBufferSharedPtr ibuf = data->indexBuffer;

	//Test if we need to read 16 or 32 bit indexes
	if (ibuf->getType() == v1::HardwareIndexBuffer::IT_32BIT)
		loadV1IndexBuffer<uint32_t>(ibuf, offset, previousSize, appendedIndexes);
	else
		loadV1IndexBuffer<uint16_t>(ibuf, offset, previousSize, appendedIndexes);
}

Real VertexIndexToShape::getRadius()
{
	if (mBoundRadius == (-1))
	{
		getSize();
		mBoundRadius = (std::max(mBounds.x, std::max(mBounds.y, mBounds.z)) * 0.5);
	}
	return mBoundRadius;
}

Vector3 VertexIndexToShape::getSize()
{
	const unsigned int vCount = getVertexCount();
	if (mBounds == Vector3(-1, -1, -1) && vCount > 0)
	{
		const Vector3 * const v = getVertices();

		Vector3 vmin(v[0]);
		Vector3 vmax(v[0]);

		for (unsigned int j = 1; j < vCount; j++)
		{
			vmin.x = std::min(vmin.x, v[j].x);
			vmin.y = std::min(vmin.y, v[j].y);
			vmin.z = std::min(vmin.z, v[j].z);

			vmax.x = std::max(vmax.x, v[j].x);
			vmax.y = std::max(vmax.y, v[j].y);
			vmax.z = std::max(vmax.z, v[j].z);
		}

		mBounds.x = vmax.x - vmin.x;
		mBounds.y = vmax.y - vmin.y;
		mBounds.z = vmax.z - vmin.z;
	}

	return mBounds;
}

//These should be const
const Vector3* VertexIndexToShape::getVertices()
{
	return mVertexBuffer.data();
}
unsigned int VertexIndexToShape::getVertexCount()
{
	return mVertexBuffer.size();
}
const unsigned int* VertexIndexToShape::getIndices()
{
	return mIndexBuffer.data();
}
unsigned int VertexIndexToShape::getIndexCount()
{
	return mIndexBuffer.size();
}

inline unsigned VertexIndexToShape::getTriangleCount()
{
	return getIndexCount() / 3;
}

btSphereShape* VertexIndexToShape::createSphere()
{
	const Real rad = getRadius();
	assert((rad > 0.0) &&
		("Sphere radius must be greater than zero"));
	btSphereShape* shape = new btSphereShape(rad);

	shape->setLocalScaling(Convert::toBullet(mScale));

	return shape;
}

btBoxShape* VertexIndexToShape::createBox()
{
	const Vector3 sz = getSize();

	assert((sz.x > 0.0) && (sz.y > 0.0) && (sz.z > 0.0) &&
		("Size of box must be greater than zero on all axes"));

	btBoxShape* shape = new btBoxShape(Convert::toBullet(sz * 0.5));

	shape->setLocalScaling(Convert::toBullet(mScale));

	return shape;
}

btCylinderShape* VertexIndexToShape::createCylinder()
{
	const Vector3 sz = getSize();

	assert((sz.x > 0.0) && (sz.y > 0.0) && (sz.z > 0.0) &&
		("Size of Cylinder must be greater than zero on all axes"));

	btCylinderShape* shape = new btCylinderShapeX(Convert::toBullet(sz * 0.5));

	shape->setLocalScaling(Convert::toBullet(mScale));

	return shape;
}
btConvexHullShape* VertexIndexToShape::createConvex()
{
	assert(getVertexCount() && (getIndexCount() >= 6) &&
		("Mesh must have some vertices and at least 6 indices (2 triangles)"));

	btConvexHullShape* shape = new btConvexHullShape(static_cast<btScalar*>(&mVertexBuffer[0].x), getVertexCount(), sizeof(Vector3));

	shape->setLocalScaling(Convert::toBullet(mScale));

	return shape;
}
btBvhTriangleMeshShape* VertexIndexToShape::createTrimesh()
{
	assert(getVertexCount() && (getIndexCount() >= 6) &&
		("Mesh must have some vertices and at least 6 indices (2 triangles)"));

	//Todo: create a "get triangle count" method
	unsigned int numFaces = getTriangleCount();

	btTriangleMesh *trimesh = new btTriangleMesh();

	unsigned int *indices = const_cast<unsigned int*>(getIndices());
	const Vector3 *vertices = getVertices();

	btVector3 vertexPos[3];
	for (unsigned int n = 0; n < numFaces; ++n)
	{
		{
			const Vector3 &vec = vertices[*indices];
			vertexPos[0][0] = vec.x;
			vertexPos[0][1] = vec.y;
			vertexPos[0][2] = vec.z;
		}
		{
			const Vector3 &vec = vertices[*(indices + 1)];
			vertexPos[1][0] = vec.x;
			vertexPos[1][1] = vec.y;
			vertexPos[1][2] = vec.z;
		}
		{
			const Vector3 &vec = vertices[*(indices + 2)];
			vertexPos[2][0] = vec.x;
			vertexPos[2][1] = vec.y;
			vertexPos[2][2] = vec.z;
		}

		indices += 3;

		trimesh->addTriangle(vertexPos[0], vertexPos[1], vertexPos[2]);
	}

	const bool useQuantizedAABB = true;
	btBvhTriangleMeshShape *shape = new btBvhTriangleMeshShape(trimesh, useQuantizedAABB);

	shape->setLocalScaling(Convert::toBullet(mScale));

	return shape;
}
btCapsuleShape* VertexIndexToShape::createCapsule() {
	const Vector3 sz = getSize();

	assert((sz.x > 0.0) && (sz.y > 0.0) && (sz.z > 0.0) &&
		("Size of the capsule must be greater than zero on all axes"));

	btScalar height = std::max(sz.x, std::max(sz.y, sz.z));
	btScalar radius;
	btCapsuleShape* shape;
	// Orient the capsule such that its axiz is aligned with the largest dimension.
	if (height == sz.y)
	{
		radius = std::max(sz.x, sz.z);
		shape = new btCapsuleShape(radius *0.5, height *0.5);
	}
	else if (height == sz.x) {
		radius = std::max(sz.y, sz.z);
		shape = new btCapsuleShapeX(radius *0.5, height *0.5);
	}
	else {
		radius = std::max(sz.x, sz.y);
		shape = new btCapsuleShapeZ(radius *0.5, height *0.5);
	}

	shape->setLocalScaling(Convert::toBullet(mScale));

	return shape;
}
VertexIndexToShape::~VertexIndexToShape()
{
	if (mBoneIndex)
	{
		for (BoneIndex::iterator i = mBoneIndex->begin();
			i != mBoneIndex->end();
			++i)
		{
			delete i->second;
		}
		delete mBoneIndex;
	}
}
VertexIndexToShape::VertexIndexToShape(const Matrix4 &transform) :
	mBounds(Vector3(-1, -1, -1)),
	mBoundRadius(-1),
	mBoneIndex(nullptr),
	mTransform(transform),
	mScale(1)
{
}

/*
 * =============================================================================================
 * BtOgre::StaticMeshToShapeConverter
 * =============================================================================================
 */

StaticMeshToShapeConverter::StaticMeshToShapeConverter() :
	VertexIndexToShape(),
	mEntity(nullptr),
	mNode(nullptr)
{
}
StaticMeshToShapeConverter::StaticMeshToShapeConverter(v1::Entity *entity, const Matrix4 &transform) :
	VertexIndexToShape(transform),
	mEntity(nullptr),
	mNode(nullptr)
{
	log("static mesh to shape converter : added entity " + entity->getName());
	addEntity(entity, transform);
}
StaticMeshToShapeConverter::StaticMeshToShapeConverter(v1::Mesh *mesh, const Matrix4 &transform) :
	VertexIndexToShape(transform),
	mEntity(nullptr),
	mNode(nullptr)
{
	log("static mesh to shape converter : added mesh " + mesh->getName());
	addMesh(mesh, transform);
}
StaticMeshToShapeConverter::StaticMeshToShapeConverter(Renderable *rend, const Matrix4 &transform) :
	VertexIndexToShape(transform),
	mEntity(nullptr),
	mNode(nullptr)
{
	v1::RenderOperation op;
	rend->getRenderOperation(op, false);
	appendVertexData(op.vertexData);
	if (op.useIndexes)
		appendIndexData(op.indexData);
}
void StaticMeshToShapeConverter::addEntity(v1::Entity *entity, const Matrix4 &transform)
{
	// Each entity added need to reset size and radius
	// next time getRadius and getSize are asked, they're computed.
	mBounds = Vector3(-1, -1, -1);
	mBoundRadius = -1;

	mEntity = entity;
	mNode = static_cast<SceneNode*>(mEntity->getParentNode());
	mTransform = transform;
	mScale = mNode ? mNode->getScale() : Vector3(1, 1, 1);

	if (mEntity->getMesh()->sharedVertexData[0])
	{
		appendVertexData(mEntity->getMesh()->sharedVertexData[0]);
	}

	for (unsigned int i = 0; i < mEntity->getNumSubEntities(); ++i)
	{
		v1::SubMesh *sub_mesh = mEntity->getSubEntity(i)->getSubMesh();

		if (!sub_mesh->useSharedVertices)
		{
			appendIndexData(sub_mesh->indexData[0], getVertexCount());
			appendVertexData(sub_mesh->vertexData[0]);
		}
		else
		{
			appendIndexData(sub_mesh->indexData[0]);
		}
	}
}
void StaticMeshToShapeConverter::addMesh(const v1::Mesh *mesh, const Matrix4 &transform)
{
	// Each entity added need to reset size and radius
	// next time getRadius and getSize are asked, they're computed.
	mBounds = Vector3(-1, -1, -1);
	mBoundRadius = -1;

	//_entity = entity;
	//_node = (SceneNode*)(_entity->getParentNode());
	mTransform = transform;

	if (mesh->hasSkeleton())
		LogManager::getSingleton().logMessage("MeshToShapeConverter::addMesh : Mesh " + mesh->getName() + " as skeleton but added to trimesh non animated");

	if (mesh->sharedVertexData[0])
	{
		appendVertexData(mesh->sharedVertexData[0]);
	}

	for (unsigned int i = 0; i < mesh->getNumSubMeshes(); ++i)
	{
		v1::SubMesh *sub_mesh = mesh->getSubMesh(i);

		if (!sub_mesh->useSharedVertices)
		{
			appendIndexData(sub_mesh->indexData[0], getVertexCount());
			appendVertexData(sub_mesh->vertexData[0]);
		}
		else
		{
			appendIndexData(sub_mesh->indexData[0]);
		}
	}
}

/*
 * =============================================================================================
 * BtOgre::AnimatedMeshToShapeConverter
 * =============================================================================================
 */

AnimatedMeshToShapeConverter::AnimatedMeshToShapeConverter(v1::Entity *entity, const Matrix4 &transform) :
	VertexIndexToShape(transform),
	mEntity(nullptr),
	mNode(nullptr),
	mTransformedVerticesTemp(nullptr),
	mTransformedVerticesTempSize(0)
{
	addEntity(entity, transform);
}
//------------------------------------------------------------------------------------------------
AnimatedMeshToShapeConverter::AnimatedMeshToShapeConverter() :
	VertexIndexToShape(),
	mEntity(nullptr),
	mNode(nullptr),
	mTransformedVerticesTemp(nullptr),
	mTransformedVerticesTempSize(0)
{
}
//------------------------------------------------------------------------------------------------
AnimatedMeshToShapeConverter::~AnimatedMeshToShapeConverter()
{
	delete[] mTransformedVerticesTemp;
}
//------------------------------------------------------------------------------------------------
void AnimatedMeshToShapeConverter::addEntity(v1::Entity *entity, const Matrix4 &transform)
{
	// Each entity added need to reset size and radius
	// next time getRadius and getSize are asked, they're computed.
	mBounds = Vector3(-1, -1, -1);
	mBoundRadius = -1;

	mEntity = entity;
	mNode = static_cast<SceneNode*>(mEntity->getParentNode());
	mTransform = transform;

	assert(entity->getMesh()->hasSkeleton());

	mEntity->addSoftwareAnimationRequest(false);
	mEntity->_updateAnimation();

	if (mEntity->getMesh()->sharedVertexData[0])
	{
		addAnimatedVertexData(mEntity->getMesh()->sharedVertexData[0],
			mEntity->_getSkelAnimVertexData(),
			&mEntity->getMesh()->sharedBlendIndexToBoneIndexMap);
	}

	for (unsigned int i = 0; i < mEntity->getNumSubEntities(); ++i)
	{
		v1::SubMesh *sub_mesh = mEntity->getSubEntity(i)->getSubMesh();

		if (!sub_mesh->useSharedVertices)
		{
			appendIndexData(sub_mesh->indexData[0], getVertexCount());

			addAnimatedVertexData(sub_mesh->vertexData[0],
				mEntity->getSubEntity(i)->_getSkelAnimVertexData(),
				&sub_mesh->blendIndexToBoneIndexMap);
		}
		else
		{
			appendIndexData(sub_mesh->indexData[0]);
		}
	}

	mEntity->removeSoftwareAnimationRequest(false);
}
//------------------------------------------------------------------------------------------------
void AnimatedMeshToShapeConverter::addMesh(const v1::MeshPtr &mesh, const Matrix4 &transform)
{
	// Each entity added need to reset size and radius
	// next time getRadius and getSize are asked, they're computed.
	mBounds = Vector3(-1, -1, -1);
	mBoundRadius = -1;

	//_entity = entity;
	//_node = (SceneNode*)(_entity->getParentNode());
	mTransform = transform;

	assert(mesh->hasSkeleton());

	if (mesh->sharedVertexData[0])
	{
		addAnimatedVertexData(mesh->sharedVertexData[0],
			nullptr,
			&mesh->sharedBlendIndexToBoneIndexMap);
	}

	for (unsigned int i = 0; i < mesh->getNumSubMeshes(); ++i)
	{
		v1::SubMesh *sub_mesh = mesh->getSubMesh(i);

		if (!sub_mesh->useSharedVertices)
		{
			appendIndexData(sub_mesh->indexData[0], getVertexCount());

			addAnimatedVertexData(sub_mesh->vertexData[0],
				nullptr,
				&sub_mesh->blendIndexToBoneIndexMap);
		}
		else
		{
			appendIndexData(sub_mesh->indexData[0]);
		}
	}
}
//------------------------------------------------------------------------------------------------
bool AnimatedMeshToShapeConverter::getBoneVertices(unsigned char bone,
	unsigned int &vertex_count,
	Vector3* &vertices,
	const Vector3 &bonePosition)
{
	BoneIndex::iterator i = mBoneIndex->find(bone);

	if (i == mBoneIndex->end())
		return false;

	if (i->second->empty())
		return false;

	vertex_count = static_cast<unsigned int>(i->second->size()) + 1;
	if (vertex_count > mTransformedVerticesTempSize)
	{
		if (mTransformedVerticesTemp)
			delete[] mTransformedVerticesTemp;

		mTransformedVerticesTemp = new Vector3[vertex_count];
	}

	vertices = mTransformedVerticesTemp;
	vertices[0] = bonePosition;
	//mEntity->_getParentNodeFullTransform() *
	//	mEntity->getSkeleton()->getBone(bone)->_getDerivedPosition();

	//mEntity->getSkeleton()->getBone(bone)->_getDerivedOrientation()
	unsigned int currBoneVertex = 1;
	Vector3Array::iterator j = i->second->begin();
	while (j != i->second->end())
	{
		vertices[currBoneVertex] = (*j);
		++j;
		++currBoneVertex;
	}
	return true;
}
//------------------------------------------------------------------------------------------------
btBoxShape* AnimatedMeshToShapeConverter::createAlignedBox(unsigned char bone,
	const Vector3 &bonePosition,
	const Quaternion &boneOrientation)
{
	unsigned int vertex_count;
	Vector3* vertices;

	if (!getBoneVertices(bone, vertex_count, vertices, bonePosition))
		return nullptr;

	Vector3 min_vec(vertices[0]);
	Vector3 max_vec(vertices[0]);

	for (unsigned int j = 1; j < vertex_count; j++)
	{
		min_vec.x = std::min(min_vec.x, vertices[j].x);
		min_vec.y = std::min(min_vec.y, vertices[j].y);
		min_vec.z = std::min(min_vec.z, vertices[j].z);

		max_vec.x = std::max(max_vec.x, vertices[j].x);
		max_vec.y = std::max(max_vec.y, vertices[j].y);
		max_vec.z = std::max(max_vec.z, vertices[j].z);
	}
	const Vector3 maxMinusMin(max_vec - min_vec);
	btBoxShape* box = new btBoxShape(Convert::toBullet(maxMinusMin));

	/*const Ogre::Vector3 pos
		(min_vec.x + (maxMinusMin.x * 0.5),
		min_vec.y + (maxMinusMin.y * 0.5),
		min_vec.z + (maxMinusMin.z * 0.5));*/

		//box->setPosition(pos);

	return box;
}
//------------------------------------------------------------------------------------------------
bool AnimatedMeshToShapeConverter::getOrientedBox(unsigned char bone,
	const Vector3 &bonePosition,
	const Quaternion &boneOrientation,
	Vector3 &box_afExtent,
	Vector3 *box_akAxis,
	Vector3 &box_kCenter)
{
	unsigned int vertex_count;
	Vector3* vertices;

	if (!getBoneVertices(bone, vertex_count, vertices, bonePosition))
		return false;

	box_kCenter = Vector3::ZERO;

	{
		for (unsigned int c = 0; c < vertex_count; c++)
		{
			box_kCenter += vertices[c];
		}
		const Real invVertexCount = 1.0 / vertex_count;
		box_kCenter *= invVertexCount;
	}
	Quaternion orient = boneOrientation;
	orient.ToAxes(box_akAxis);

	// Let C be the box center and let U0, U1, and U2 be the box axes. Each
	// input point is of the form X = C + y0*U0 + y1*U1 + y2*U2.  The
	// following code computes min(y0), max(y0), min(y1), max(y1), min(y2),
	// and max(y2).  The box center is then adjusted to be
	// C' = C + 0.5*(min(y0)+max(y0))*U0 + 0.5*(min(y1)+max(y1))*U1 +
	//      0.5*(min(y2)+max(y2))*U2

	Vector3 kDiff(vertices[1] - box_kCenter);
	Real fY0Min = kDiff.dotProduct(box_akAxis[0]), fY0Max = fY0Min;
	Real fY1Min = kDiff.dotProduct(box_akAxis[1]), fY1Max = fY1Min;
	Real fY2Min = kDiff.dotProduct(box_akAxis[2]), fY2Max = fY2Min;

	for (unsigned int i = 2; i < vertex_count; i++)
	{
		kDiff = vertices[i] - box_kCenter;

		const Real fY0 = kDiff.dotProduct(box_akAxis[0]);
		if (fY0 < fY0Min)
			fY0Min = fY0;
		else if (fY0 > fY0Max)
			fY0Max = fY0;

		const Real fY1 = kDiff.dotProduct(box_akAxis[1]);
		if (fY1 < fY1Min)
			fY1Min = fY1;
		else if (fY1 > fY1Max)
			fY1Max = fY1;

		const Real fY2 = kDiff.dotProduct(box_akAxis[2]);
		if (fY2 < fY2Min)
			fY2Min = fY2;
		else if (fY2 > fY2Max)
			fY2Max = fY2;
	}

	box_afExtent.x = Real(0.5)*(fY0Max - fY0Min);
	box_afExtent.y = Real(0.5)*(fY1Max - fY1Min);
	box_afExtent.z = Real(0.5)*(fY2Max - fY2Min);

	box_kCenter += 0.5*(fY0Max + fY0Min)*box_akAxis[0] +
		0.5*(fY1Max + fY1Min)*box_akAxis[1] +
		0.5*(fY2Max + fY2Min)*box_akAxis[2];

	box_afExtent *= 2.0;

	return true;
}
//------------------------------------------------------------------------------------------------
btBoxShape *AnimatedMeshToShapeConverter::createOrientedBox(unsigned char bone,
	const Vector3 &bonePosition,
	const Quaternion &boneOrientation)
{
	Vector3 box_akAxis[3];
	Vector3 box_afExtent;
	Vector3 box_afCenter;

	if (!getOrientedBox(bone, bonePosition, boneOrientation,
		box_afExtent,
		box_akAxis,
		box_afCenter))
		return nullptr;

	btBoxShape *geom = new btBoxShape(Convert::toBullet(box_afExtent));
	//geom->setOrientation(Quaternion(box_akAxis[0],box_akAxis[1],box_akAxis[2]));
	//geom->setPosition(box_afCenter);
	return geom;
}