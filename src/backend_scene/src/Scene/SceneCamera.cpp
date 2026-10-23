#include "SceneCamera.h"
#include "SceneNode.h"
#include "Utils/Logging.h"
#include <iostream>
#include "Utils/Eigen.h"

using namespace wallpaper;
using namespace Eigen;

Vector3d SceneCamera::GetPosition() const {
	if(m_lookAt) return m_eye;
	if(m_node) {
		return Affine3d(m_node->GetLocalTrans()) * Vector3d::Zero();
	}
	return Vector3d::Zero();
}

Vector3d SceneCamera::GetDirection() const {
	if(m_lookAt) return (m_center - m_eye).normalized();
	if(m_node) {
		return (m_node->GetLocalTrans() * Vector4d(0.0f, 0.0f, -1.0f, 0.0f)).head<3>();
	}
	return -Vector3d::UnitZ();
}

Matrix4d SceneCamera::GetViewMatrix() const {
	return m_viewMat;
}

Matrix4d SceneCamera::GetViewProjectionMatrix() const {
	return m_viewProjectionMat;
}

void SceneCamera::CalculateViewProjectionMatrix() {
	// CalculateViewMatrix
	{
		if(m_lookAt) {
			m_viewMat = LookAt(m_eye, m_center, m_up);
			if(m_mirrorY) m_viewMat = m_viewMat * Vector4d(1, -1, 1, 1).asDiagonal().toDenseMatrix();
		} else if(m_node) {
			Affine3d nodeTrans(m_node->GetLocalTrans());
			Vector3d eye = nodeTrans * Vector3d::Zero();
			Vector3d center = nodeTrans * (-Vector3d::UnitZ());
			Vector3d up = Vector3d::UnitY();
			m_viewMat = LookAt(eye, center, up);
		} else 
			m_viewMat = Matrix4d::Identity();
	};

	if(m_perspective) {
		Matrix4d proj = Perspective(Radians(m_fov), m_aspect, m_nearClip, m_farClip);
		if(m_mirrorY) {
			// Only what is above the mirror plane (y = 0) is reflected: move the near plane
			// onto it (oblique near plane, depth in [0, 1]).
			Vector4d plane = m_viewMat.inverse().transpose() * Vector4d(0, 1, 0, 0);
			auto     sgn   = [](double v) { return v > 0 ? 1.0 : v < 0 ? -1.0 : 0.0; };
			Vector4d q     = proj.inverse() * Vector4d(sgn(plane.x()), sgn(plane.y()), 1, 1);
			proj.row(2)    = plane.transpose() / plane.dot(q);
			// Shaders sample the reflection with D3D screen coordinates (v down), so it is
			// stored upside down. This also undoes the winding flip of the mirror.
			proj = Vector4d(1, -1, 1, 1).asDiagonal().toDenseMatrix() * proj;
		}
		m_viewProjectionMat = proj * m_viewMat;
	} else {
		double left = -m_width/2.0f;
		double right = m_width/2.0f;
		double bottom = -m_height/2.0f;
		double up = m_height/2.0f;
		m_viewProjectionMat = Ortho(left, right, bottom, up, m_nearClip, m_farClip) * m_viewMat;
	}
}

void SceneCamera::Update() {
	CalculateViewProjectionMatrix();
}


void SceneCamera::SetLookAt(const Vector3d& eye, const Vector3d& center, const Vector3d& up) {
	m_lookAt = true;
	m_eye    = eye;
	m_center = center;
	m_up     = up;
	Update();
}
void SceneCamera::AttatchNode(std::shared_ptr<SceneNode> node) {
	if(!node) {
		LOG_ERROR("Attach a null node to camera");		
		return;
	}
	m_node = node;
	Update();
}