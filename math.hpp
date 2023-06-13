#ifndef ADHTP_MATH_HDR
#define ADHTP_MATH_HDR

struct Vector2D {
    float x;
    float y;
    Vector2D(float x, float y) : x(x), y(y) {}
    Vector2D operator-(const Vector2D& other) const;
    Vector2D operator-() const;
    Vector2D operator*(float scalar) const;
    float dot(const Vector2D& other) const;
    float length() const;
    void normalize();
};

Vector2D reflect(const Vector2D& vect, const Vector2D& surf_norm);

#endif // ADHTP_NETWORK_HDR
