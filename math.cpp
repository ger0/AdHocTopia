#include "math.hpp"
#include <cmath>

Vector2D Vector2D::operator-(const Vector2D& other) const {
    return Vector2D(x - other.x, y - other.y);
}
Vector2D Vector2D::operator-() const {
    return Vector2D(-x, -y);
}
Vector2D Vector2D::operator*(float scalar) const {
    return Vector2D(x * scalar, y * scalar);
}
float Vector2D::dot(const Vector2D& other) const {
    return x * other.x + y * other.y;
}
float Vector2D::length() const {
    return std::sqrt(x * x + y * y);
}
void Vector2D::normalize() {
    float len = length();
    if (len != 0.0f) {
        x /= len;
        y /= len;
    }
}

Vector2D reflect(const Vector2D& incidentRay, const Vector2D& surfaceNormal) {
    Vector2D incidentVector = -incidentRay;
    incidentVector.normalize();

    float dotProduct = incidentVector.dot(surfaceNormal);
    Vector2D reflectionVector = incidentVector - surfaceNormal * (2.0f * dotProduct);

    reflectionVector.normalize();
    return reflectionVector;
}
