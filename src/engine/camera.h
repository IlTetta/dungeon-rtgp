#pragma once

// Camera class (first person / FPS style).
// It keeps the camera position and orientation, and it builds the "view matrix" that the
// shaders need (the matrix that transforms the world into the camera point of view).

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>   // glm::lookAt

// The directions the camera can move, used by processKeyboard().
enum CameraMovement {
    FORWARD,
    BACKWARD,
    LEFT,
    RIGHT
};

// default values
const float YAW         = -90.0f;   // initial horizontal angle (looking toward -Z)
const float PITCH       =  0.0f;    // initial vertical angle
const float SPEED       =  3.0f;    // movement speed (units per second)
const float SENSITIVITY =  0.25f;   // how much the mouse movement counts

class Camera {
public:
    // camera vectors
    glm::vec3 Position;
    glm::vec3 Front;        // where the camera looks
    glm::vec3 WorldFront;   // like Front but flattened on the ground (y = 0), for walking
    glm::vec3 Up;
    glm::vec3 Right;
    glm::vec3 WorldUp;      // the world "up" (0,1,0), used to rebuild the other vectors

    // if true, the camera walks on the ground (forward does not make it fly up/down);
    // if false, it moves freely along the Front direction.
    bool onGround;

    // orientation angles (Euler angles)
    float Yaw;
    float Pitch;

    float MovementSpeed;
    float MouseSensitivity;

    Camera(glm::vec3 position, bool onGround)
        : Position(position), onGround(onGround),
          Yaw(YAW), Pitch(PITCH), MovementSpeed(SPEED), MouseSensitivity(SENSITIVITY)
    {
        WorldUp = glm::vec3(0.0f, 1.0f, 0.0f);
        updateVectors();   // compute Front / Right / Up from Yaw and Pitch
    }

    // The view matrix. lookAt needs: where the camera is, a point it looks at
    // (position + front), and which way is up.
    glm::mat4 getViewMatrix() {
        return glm::lookAt(Position, Position + Front, Up);
    }

    // Called when a WASD key is held. deltaTime makes the movement independent from the
    // frame rate (so the camera moves at the same real speed on a fast or slow machine).
    void processKeyboard(CameraMovement direction, float deltaTime) {
        float velocity = MovementSpeed * deltaTime;

        // when walking on the ground we move along WorldFront (no vertical component),
        // otherwise along the real Front (we can fly).
        if (direction == FORWARD)
            Position += (onGround ? WorldFront : Front) * velocity;
        if (direction == BACKWARD)
            Position -= (onGround ? WorldFront : Front) * velocity;
        if (direction == LEFT)
            Position -= Right * velocity;
        if (direction == RIGHT)
            Position += Right * velocity;
    }

    // Called when the mouse moves. xoffset / yoffset are how much the mouse moved since
    // the previous frame.
    void processMouse(float xoffset, float yoffset) {
        xoffset *= MouseSensitivity;
        yoffset *= MouseSensitivity;

        Yaw   += xoffset;
        Pitch += yoffset;

        // clamp the vertical angle so we cannot flip the camera upside down
        if (Pitch > 89.0f)  Pitch = 89.0f;
        if (Pitch < -89.0f) Pitch = -89.0f;

        updateVectors();
    }

private:
    // Rebuild Front / Right / Up (and WorldFront) from the current Yaw and Pitch.
    void updateVectors() {
        // standard formula to get the looking direction from the two angles
        glm::vec3 front;
        front.x = cos(glm::radians(Yaw)) * cos(glm::radians(Pitch));
        front.y = sin(glm::radians(Pitch));
        front.z = sin(glm::radians(Yaw)) * cos(glm::radians(Pitch));
        Front = glm::normalize(front);

        // WorldFront is Front but flattened on the ground (used when onGround is true)
        front.y = 0.0f;
        WorldFront = glm::normalize(front);

        // Right is perpendicular to Front and to the world up; Up is perpendicular to both
        Right = glm::normalize(glm::cross(Front, WorldUp));
        Up    = glm::normalize(glm::cross(Right, Front));
    }
};
