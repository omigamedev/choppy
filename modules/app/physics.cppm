module;

#include <cstdio>
#include <fstream>
#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyActivationListener.h>
#include <Jolt/Renderer/DebugRendererRecorder.h>
#include <Jolt/Core/StreamWrapper.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/Character/Character.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>

#ifdef __ANDROID__
#include <android/log.h>
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "ChoppyEngine", __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "ChoppyEngine", __VA_ARGS__)
#elifdef _WIN32
#define LOGE(fmt, ...) printf(fmt "\n", ##__VA_ARGS__)
#define LOGI(fmt, ...) printf(fmt "\n", ##__VA_ARGS__)
#endif

export module ce.app:physics;
import glm;
import :ecs;
import :chunkgen;

namespace Layers
{
static constexpr JPH::ObjectLayer NON_MOVING = 0;
static constexpr JPH::ObjectLayer MOVING = 1;
static constexpr JPH::ObjectLayer NUM_LAYERS = 2;
};
namespace BroadPhaseLayers
{
static constexpr JPH::BroadPhaseLayer NON_MOVING(0);
static constexpr JPH::BroadPhaseLayer MOVING(1);
static constexpr JPH::uint NUM_LAYERS(2);
};

export namespace ce::app::physics
{
/// Class that determines if two object layers can collide
class ObjectLayerPairFilterImpl : public JPH::ObjectLayerPairFilter
{
public:
	bool ShouldCollide(JPH::ObjectLayer inObject1, JPH::ObjectLayer inObject2) const override
	{
		switch (inObject1)
		{
		case Layers::NON_MOVING:
			return inObject2 == Layers::MOVING; // Non moving only collides with moving
		case Layers::MOVING:
			return true; // Moving collides with everything
		default:
			JPH_ASSERT(false);
			return false;
		}
	}
};

// BroadPhaseLayerInterface implementation
// This defines a mapping between object and broadphase layers.
class BPLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface
{
public:
    BPLayerInterfaceImpl()
	{
		// Create a mapping table from object to broad phase layer
		mObjectToBroadPhase[Layers::NON_MOVING] = BroadPhaseLayers::NON_MOVING;
		mObjectToBroadPhase[Layers::MOVING] = BroadPhaseLayers::MOVING;
	}

	JPH::uint GetNumBroadPhaseLayers() const override
	{
		return BroadPhaseLayers::NUM_LAYERS;
	}

	JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer inLayer) const override
	{
		JPH_ASSERT(inLayer < Layers::NUM_LAYERS);
		return mObjectToBroadPhase[inLayer];
	}

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
	const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer inLayer) const override
	{
		switch ((JPH::BroadPhaseLayer::Type)inLayer)
		{
		case (JPH::BroadPhaseLayer::Type)BroadPhaseLayers::NON_MOVING:	return "NON_MOVING";
		case (JPH::BroadPhaseLayer::Type)BroadPhaseLayers::MOVING:		return "MOVING";
		default:													JPH_ASSERT(false); return "INVALID";
		}
	}
#endif // JPH_EXTERNAL_PROFILE || JPH_PROFILE_ENABLED

private:
	JPH::BroadPhaseLayer mObjectToBroadPhase[Layers::NUM_LAYERS];
};

/// Class that determines if an object layer can collide with a broadphase layer
class ObjectVsBroadPhaseLayerFilterImpl : public JPH::ObjectVsBroadPhaseLayerFilter
{
public:
	bool ShouldCollide(JPH::ObjectLayer inLayer1, JPH::BroadPhaseLayer inLayer2) const override
	{
		switch (inLayer1)
		{
		case Layers::NON_MOVING:
			return inLayer2 == BroadPhaseLayers::MOVING;
		case Layers::MOVING:
			return true;
		default:
			JPH_ASSERT(false);
			return false;
		}
	}
};

// An example contact listener
class MyContactListener : public JPH::ContactListener
{
public:
	// See: ContactListener
    JPH::ValidateResult	OnContactValidate(const JPH::Body &inBody1,
        const JPH::Body &inBody2, JPH::RVec3Arg inBaseOffset, const JPH::CollideShapeResult &inCollisionResult) override
	{
		// LOGI("Contact validate callback");

		// Allows you to ignore a contact before it is created (using layers to not make objects collide is cheaper!)
		return JPH::ValidateResult::AcceptAllContactsForThisBodyPair;
	}

	void OnContactAdded(const JPH::Body &inBody1, const JPH::Body &inBody2, const JPH::ContactManifold &inManifold, JPH::ContactSettings &ioSettings) override
	{
		// LOGI("A contact was added");
	}

	void OnContactPersisted(const JPH::Body &inBody1, const JPH::Body &inBody2, const JPH::ContactManifold &inManifold, JPH::ContactSettings &ioSettings) override
	{
		// LOGI("A contact was persisted");
	}

	void OnContactRemoved(const JPH::SubShapeIDPair &inSubShapePair) override
	{
		// LOGI("A contact was removed");
	}
};

// An example activation listener
class MyBodyActivationListener : public JPH::BodyActivationListener
{
public:
	void OnBodyActivated(const JPH::BodyID &inBodyID, JPH::uint64 inBodyUserData) override
	{
		LOGI("A body got activated");
	}

	void OnBodyDeactivated(const JPH::BodyID &inBodyID, JPH::uint64 inBodyUserData) override
	{
		LOGI("A body went to sleep");
	}
};
class PhysicsSystem : public ecs::System
{
	std::unique_ptr<JPH::JobSystemThreadPool> job_system;
	std::unique_ptr<JPH::TempAllocatorImpl> temp_allocator;
	JPH::PhysicsSystem physics_system;
	BPLayerInterfaceImpl broad_phase_layer_interface;
	ObjectVsBroadPhaseLayerFilterImpl object_vs_broadphase_layer_filter;
	ObjectLayerPairFilterImpl object_vs_object_layer_filter;
	// MyBodyActivationListener body_activation_listener;
	// MyContactListener contact_listener;
	JPH::RefConst<JPH::Shape> shared_box_shape;

	bool is_recording = false;
	std::unique_ptr<JPH::DebugRendererRecorder> recorder;
	std::unique_ptr<JPH::StreamOutWrapper> recorder_stream;
	std::ofstream recorder_file;

	const uint32_t physMaxBodies = 4096;
	const uint32_t physNumBodyMutexes = 0;
	const uint32_t physMaxBodyPairs = 1024;
	const uint32_t physMaxContactConstraints = 1024;
	const uint32_t physCollisionSteps = 1;
public:
	bool create_system() noexcept
	{
		JPH::RegisterDefaultAllocator();
        JPH::Factory::sInstance = new JPH::Factory();
        JPH::RegisterTypes();

        job_system = std::make_unique<JPH::JobSystemThreadPool>(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers,
            static_cast<int32_t>(std::thread::hardware_concurrency()) - 1);
        physics_system.Init(physMaxBodies, physMaxBodies, physMaxBodies, physMaxBodies,
            broad_phase_layer_interface, object_vs_broadphase_layer_filter, object_vs_object_layer_filter);
        // physics_system.SetBodyActivationListener(&body_activation_listener);
        // physics_system.SetContactListener(&contact_listener);
        temp_allocator = std::make_unique<JPH::TempAllocatorImpl>(10 * 1024 * 1024);
		return true;
	}
	bool create_shared_box(const float block_size) noexcept
	{
		float hs = block_size * 0.5f;
		const auto box_settings = new JPH::BoxShapeSettings({hs, hs, hs});
		if (const auto result = box_settings->Create(); result.IsValid())
		{
			shared_box_shape = result.Get();
			return true;
		}
		return false;
	}
	JPH::Ref<JPH::Character> create_character() noexcept
	{
		// Constants for the character
		constexpr float cCharacterHeight = 1.7f; // Total character height
		constexpr float cCharacterRadius = 0.15f;
		constexpr float cMaxSlopeAngle = glm::radians(45.0f);
		const JPH::RefConst character_shape = new JPH::CapsuleShape(
			cCharacterHeight / 2.0f - cCharacterRadius, cCharacterRadius);

		// 2. Create the CharacterSettings
		const JPH::Ref settings = new JPH::CharacterSettings();
		settings->mMass = 40.0f; // Player mass
		settings->mShape = character_shape;
		settings->mMaxSlopeAngle = cMaxSlopeAngle;
		settings->mLayer = Layers::MOVING; // Your custom object layer for players
		settings->mUp = JPH::Vec3::sAxisY(); // Gravity direction is always Y-axis for Jolt characters

		// Optional settings:
		// settings->mMaxStrength = 100.0f;
		settings->mFriction = 0.0f;
		JPH::Ref character = new JPH::Character(settings, {0, 100, 0}, JPH::Quat::sIdentity(), 0, &physics_system);
		character->AddToPhysicsSystem(JPH::EActivation::Activate);
		return character;
	}
	void remove_body(JPH::BodyID& body_id) noexcept
	{
        JPH::BodyInterface& body_interface = physics_system.GetBodyInterface();
		if (body_interface.IsAdded(body_id))
		{
			body_interface.RemoveBody(body_id);
			body_interface.DestroyBody(body_id);
		}
		body_id = {};
	}
	std::optional<std::tuple<JPH::BodyID, JPH::RefConst<JPH::Shape>>> create_chunk_body(const uint32_t chunk_size,
		const float block_size, const ChunkData& data, const glm::ivec3 sector) noexcept
	{
		// Create physics body
		JPH::StaticCompoundShapeSettings compound_settings;
		for (uint32_t y = 0; y < chunk_size; ++y)
		{
			for (uint32_t z = 0; z < chunk_size; ++z)
			{
				for (uint32_t x = 0; x < chunk_size; ++x)
				{
					const auto& b = data.blocks[y * pow(chunk_size, 2) + z * chunk_size + x];
					if (b.type != BlockType::Water && b.type != BlockType::Air)
					{
						const glm::vec3 p = glm::vec3(x, y, z) * block_size + block_size * 0.5f;
						const JPH::Vec3 position = JPH::Vec3(p.x, p.y, p.z);
						compound_settings.AddShape(position, JPH::Quat::sIdentity(), shared_box_shape);
					}
				}
			}
		}
		if (const auto result = compound_settings.Create(); result.IsValid())
		{
			auto shape = result.Get();
			const glm::vec3 sector_origin = glm::vec3(sector) * block_size * chunk_size;
			const JPH::Vec3 origin = JPH::Vec3(sector_origin.x, sector_origin.y, sector_origin.z);
			const JPH::BodyCreationSettings body_settings(
			   shape,
			   origin,        // world position for the chunk body
			   JPH::Quat::sIdentity(),
			   JPH::EMotionType::Static,
			   Layers::NON_MOVING
			);
			JPH::BodyInterface& body_interface = physics_system.GetBodyInterface();
			auto body_id = body_interface.CreateAndAddBody(body_settings, JPH::EActivation::DontActivate);
			return {{body_id, shape}};
		}
		return std::nullopt;
	}
	void start_recording() noexcept
	{
		if (!is_recording)
		{
			recorder_file = std::ofstream("jolt.jor", std::ofstream::binary);
			recorder_stream = std::make_unique<JPH::StreamOutWrapper>(recorder_file);
			recorder = std::make_unique<JPH::DebugRendererRecorder>(*recorder_stream);
			is_recording = true;
		}
	}
	void tick(const float dt) noexcept
	{
		physics_system.Update(dt, 1, temp_allocator.get(), job_system.get());
		if (is_recording)
		{
			static float recorder_timer = 0;
			recorder_timer += dt;
			if (recorder_timer >= 1.0f)
			{
				constexpr JPH::BodyManager::DrawSettings settings{};
				recorder->NextFrame();
				physics_system.DrawBodies(settings, recorder.get());
				recorder->EndFrame();
				recorder_timer = 0;
			}
		}
	}
};
}