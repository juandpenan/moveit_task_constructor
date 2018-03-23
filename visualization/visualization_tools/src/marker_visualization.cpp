#include <moveit/visualization_tools/marker_visualization.h>
#include <moveit/planning_scene/planning_scene.h>

#include <rviz/default_plugin/markers/marker_base.h>
#include "rviz/default_plugin/markers/arrow_marker.h"
#include "rviz/default_plugin/markers/line_list_marker.h"
#include "rviz/default_plugin/markers/line_strip_marker.h"
#include "rviz/default_plugin/markers/mesh_resource_marker.h"
#include "rviz/default_plugin/markers/points_marker.h"
#include "rviz/default_plugin/markers/shape_marker.h"
#include "rviz/default_plugin/markers/text_view_facing_marker.h"
#include "rviz/default_plugin/markers/triangle_list_marker.h"

#include <rviz/display_context.h>
#include <rviz/frame_manager.h>
#include <ros/console.h>
#include <eigen_conversions/eigen_msg.h>
#include <OgreSceneManager.h>

namespace moveit_rviz_plugin {

rviz::MarkerBase* createMarker(int marker_type, rviz::DisplayContext* context, Ogre::SceneNode* node)
{
	switch (marker_type) {
	case visualization_msgs::Marker::CUBE:
	case visualization_msgs::Marker::CYLINDER:
	case visualization_msgs::Marker::SPHERE:
		return new rviz::ShapeMarker(nullptr, context, node);

	case visualization_msgs::Marker::ARROW:
		return new rviz::ArrowMarker(nullptr, context, node);

	case visualization_msgs::Marker::LINE_STRIP:
		return new rviz::LineStripMarker(nullptr, context, node);

	case visualization_msgs::Marker::LINE_LIST:
		return new rviz::LineListMarker(nullptr, context, node);

	case visualization_msgs::Marker::SPHERE_LIST:
	case visualization_msgs::Marker::CUBE_LIST:
	case visualization_msgs::Marker::POINTS:
		return new rviz::PointsMarker(nullptr, context, node);

	case visualization_msgs::Marker::TEXT_VIEW_FACING:
		return new rviz::TextViewFacingMarker(nullptr, context, node);

	case visualization_msgs::Marker::MESH_RESOURCE:
		return new rviz::MeshResourceMarker(nullptr, context, node);

	case visualization_msgs::Marker::TRIANGLE_LIST:
		return new rviz::TriangleListMarker(nullptr, context, node);

	default:
		ROS_ERROR("Unknown marker type: %d", marker_type);
		return nullptr;
	}
}

namespace {
// express marker pose relative to planning frame (of end scene)
bool toPlanningFrame(visualization_msgs::Marker &marker,
                     const planning_scene::PlanningScene &scene)
{
	if (marker.header.frame_id == scene.getPlanningFrame())
		return true;

	if (!scene.knowsFrameTransform(marker.header.frame_id)) {
		ROS_WARN_ONCE("unknown frame '%s' for solution marker in namespace '%s'",
		              marker.header.frame_id.c_str(), marker.ns.c_str());
		return false;
	}

	Eigen::Affine3d pose;
	tf::poseMsgToEigen(marker.pose, pose);
	const Eigen::Affine3d tm = scene.getFrameTransform(marker.header.frame_id);
	tf::poseEigenToMsg(tm * pose, marker.pose);
	marker.header.frame_id = scene.getPlanningFrame();
	return true;
}
}

MarkerVisualization::MarkerVisualization(const std::vector<visualization_msgs::Marker> &markers,
                                         const planning_scene::PlanningScene &end_scene)
{
	// remember marker message, postpone rviz::MarkerBase creation until later
	for (const auto& marker : markers) {
		// create MarkerData with nil Marker pointer
		MarkerData data;
		data.first.reset(new visualization_msgs::Marker(marker));
		// express marker pose relative to planning frame of end_scene
		if (!toPlanningFrame(const_cast<visualization_msgs::Marker&>(*data.first), end_scene))
			continue;
		markers_.push_back(std::move(data));
		// remember namespace name
		namespaces_.insert(std::make_pair(QString::fromStdString(marker.ns), nullptr));
	}
}

MarkerVisualization::~MarkerVisualization()
{
	for (const auto& pair : namespaces_) {
		if (pair.second)
			pair.second->getCreator()->destroySceneNode(pair.second);
	}
}

void setVisibility(Ogre::SceneNode *node, Ogre::SceneNode *parent, bool visible)
{
	if (visible && node->getParent() != parent)
		parent->addChild(node);
	else if (!visible && node->getParent())
		node->getParent()->removeChild(node);
}

void MarkerVisualization::setVisible(const QString &ns, Ogre::SceneNode* parent_scene_node, bool visible)
{
	auto it = namespaces_.find(ns);
	if (it == namespaces_.end())
		return;
	setVisibility(it->second, parent_scene_node, visible);
}

void MarkerVisualization::createMarkers(rviz::DisplayContext *context, Ogre::SceneNode *parent_scene_node)
{
	std::string planning_frame;
	Ogre::Quaternion quat;
	Ogre::Vector3 pos;

	for (MarkerData& data : markers_) {
		if (data.second) continue;

		auto ns_it = namespaces_.find(QString::fromStdString(data.first->ns));
		Q_ASSERT(ns_it != namespaces_.end()); // we have added all namespaces before!
		if (ns_it->second == nullptr) // create scene node for this namespace
			ns_it->second = parent_scene_node->getCreator()->createSceneNode();

		data.second.reset(createMarker(data.first->type, context, ns_it->second));
		if (data.second) {
			// rviz::MarkerBase::setMessage() initializes the marker
			data.second->setMessage(data.first);
			// ... and sets its position + orientation relative to rviz' fixed frame
			// however, we want the marker to be placed w.r.t. planning frame = msg.header.frame_id
			Q_ASSERT(!data.first->header.frame_id.empty());
			if (planning_frame.empty()) { // determine transform once
				planning_frame = data.first->header.frame_id;
				// transform from fixed frame to planning_frame
				tf::TransformListener* tf = context->getFrameManager()->getTFClient();
				tf::StampedTransform tm;
				tf->lookupTransform(context->getFrameManager()->getFixedFrame(), planning_frame, ros::Time(), tm);
				auto q = tm.getRotation();
				auto p = tm.getOrigin();
				quat = Ogre::Quaternion(q.w(), -q.x(), -q.y(), -q.z());
				pos = Ogre::Vector3(p.x(), p.y(), p.z());
			} else {
				Q_ASSERT(data.first->header.frame_id == planning_frame);
			}
			data.second->setOrientation(quat * data.second->getOrientation());
			data.second->setPosition(quat * (data.second->getPosition()  - pos));
		}
	}
}


MarkerVisualizationProperty::MarkerVisualizationProperty(const QString &name, rviz::Property *parent)
   : rviz::BoolProperty(name, true, "Enable/disable markers", parent)
{
	connect(this, SIGNAL(changed()), this, SLOT(onEnableChanged()));
}

MarkerVisualizationProperty::~MarkerVisualizationProperty()
{
	if (marker_scene_node_)
		marker_scene_node_->getCreator()->destroySceneNode(marker_scene_node_);
}

void MarkerVisualizationProperty::onInitialize(Ogre::SceneNode *scene_node, rviz::DisplayContext *context)
{
	context_ = context;
	parent_scene_node_ = scene_node;
	marker_scene_node_ = parent_scene_node_->createChildSceneNode();
}

void MarkerVisualizationProperty::clearMarkers()
{
	// detach all existing scene nodes
	marker_scene_node_->removeAllChildren();
	// clear list of hosted markers
	hosted_markers_.clear();
}

void MarkerVisualizationProperty::addMarkers(MarkerVisualizationPtr markers)
{
	if (!markers) return;

	// remember that those markers are hosted
	hosted_markers_.push_back(markers);

	// attach all scene nodes from markers
	for (const auto& pair : markers->namespaces()) {
		// create sub property for newly encountered namespace, enabling visibility by default
		auto ns_it = namespaces_.insert(std::make_pair(pair.first, nullptr)).first;
		if (ns_it->second == nullptr) {
			ns_it->second = new rviz::BoolProperty(pair.first, true, "Show/hide markers of this namespace", this,
			                                       SLOT(onNSEnableChanged()), this);
		}
		if (!pair.second) // invalid scene node indicates that we still need to create the rviz markers
			markers->createMarkers(context_, marker_scene_node_);
		Q_ASSERT(pair.second);

		if (ns_it->second->getBool())
			marker_scene_node_->addChild(pair.second);
	}
}

void MarkerVisualizationProperty::onEnableChanged()
{
	setVisibility(marker_scene_node_, parent_scene_node_, getBool());
}

void MarkerVisualizationProperty::onNSEnableChanged()
{
	rviz::BoolProperty *ns_property = static_cast<rviz::BoolProperty*>(sender());
	const QString &ns = ns_property->getName();
	bool visible = ns_property->getBool();
	// for all hosted markers, set visibility of given namespace
	for (const auto& markers : hosted_markers_)
		markers->setVisible(ns, marker_scene_node_, visible);
}

} // namespace moveit_rviz_plugin
