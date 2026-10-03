#pragma once

// First-person camera (FPS style).
// It keeps the position and the orientation (two Euler angles, Yaw and Pitch) and builds the
// view matrix, the matrix that brings the world into the camera point of view.

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

// The directions the camera can move, used by processKeyboard().
enum CameraMovement {
    FORWARD,
    BACKWARD,
    LEFT,
    RIGHT
};

// default values
const float YAW = -90.0f;   // initial horizontal angle (looking toward -Z)
const float PITCH = 0.0f;   // initial vertical angle
const float SPEED = 3.0f;   // units per second (the input code sets 6, or 15 when sprinting)
const float SENSITIVITY = 0.25f;    // how much the mouse movement counts

class Camera {
public:
    glm::vec3 Position;
    glm::vec3 Front;    // where the camera looks
    glm::vec3 WorldFront;   // Front flattened on the ground (y = 0), used for walking
    glm::vec3 Up;
    glm::vec3 Right;
    glm::vec3 WorldUp;  // the world up (0,1,0), used to rebuild the other vectors

    // true: we walk on the ground, so looking up or down does not make us fly.
    // false: we move freely along Front.
    bool onGround;

    // orientation, in degrees
    float Yaw;
    float Pitch;

    float MovementSpeed;
    float MouseSensitivity;

    Camera(glm::vec3 position, bool onGround)
        : Position(position), onGround(onGround),
          Yaw(YAW), Pitch(PITCH), MovementSpeed(SPEED), MouseSensitivity(SENSITIVITY)
    {
        WorldUp = glm::vec3(0.0f, 1.0f, 0.0f);
        updateVectors();
    }

    // lookAt needs where the camera is, a point it looks at (position + front) and the up vector
    glm::mat4 getViewMatrix() {
        return glm::lookAt(Position, Position + Front, Up);
    }

    // Called for every held WASD key. Multiplying by deltaTime makes the speed the same on a
    // fast or a slow machine. We do not normalize diagonal movement, so W+D is about 1.41 times
    // faster than W alone.
    void processKeyboard(CameraMovement direction, float deltaTime) {
        float velocity = MovementSpeed * deltaTime;

        if (direction == FORWARD)
            Position += (onGround ? WorldFront : Front) * velocity;
        if (direction == BACKWARD)
            Position -= (onGround ? WorldFront : Front) * velocity;
        if (direction == LEFT)
            Position -= Right * velocity;
        if (direction == RIGHT)
            Position += Right * velocity;
    }

    // Called when the mouse moves; the offsets are how much it moved since the last event.
    // The benchmark replay also calls it with (0, 0) just to rebuild the vectors after setting
    // Yaw and Pitch by hand.
    void processMouse(float xoffset, float yoffset) {
        xoffset *= MouseSensitivity;
        yoffset *= MouseSensitivity;

        Yaw += xoffset;
        Pitch += yoffset;

        // clamp the pitch: at +-90 degrees Front would be parallel to WorldUp and the cross
        // product in updateVectors() would break (and the view would flip)
        if (Pitch > 89.0f)
            Pitch = 89.0f;
        if (Pitch < -89.0f)
            Pitch = -89.0f;

        updateVectors();
    }

private:
    // Rebuild Front, WorldFront, Right and Up from Yaw and Pitch.
    void updateVectors() {
        // spherical to cartesian: the look direction from the two angles
        glm::vec3 front;
        front.x = cos(glm::radians(Yaw)) * cos(glm::radians(Pitch));
        front.y = sin(glm::radians(Pitch));
        front.z = sin(glm::radians(Yaw)) * cos(glm::radians(Pitch));
        Front = glm::normalize(front);

        front.y = 0.0f;
        WorldFront = glm::normalize(front);

        // Right is perpendicular to Front and to the world up; Up is perpendicular to both
        Right = glm::normalize(glm::cross(Front, WorldUp));
        Up = glm::normalize(glm::cross(Right, Front));
    }
};
