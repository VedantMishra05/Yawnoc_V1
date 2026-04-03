#include <SFML/Graphics.hpp>
#include <SFML/Audio.hpp>
#include <cmath>
#include <vector>
#include <random>
#include <fstream>
#include <string>
#include <algorithm>
#include <cstdint>
#include <memory>
#include <functional>

// ==================== CONSTANTS ====================
constexpr int   WINDOW_W        = 960;
constexpr int   WINDOW_H        = 540;
constexpr int   WORLD_W         = 960;
constexpr int   WORLD_H         = 540;
constexpr float PLAYER_SPEED    = 240.f;
constexpr float BULLET_SPEED    = 750.f;
constexpr float FIRE_DELAY      = 0.10f;
constexpr int   MAX_HEALTH      = 100;

enum class GameState { MENU, PLAYING, GAME_OVER };

// ==================== RNG ====================
static std::mt19937 rng(std::random_device{}());
float randf(float a, float b) {
    std::uniform_real_distribution<float> d(a, b);
    return d(rng);
}
int randi(int a, int b) {
    std::uniform_int_distribution<int> d(a, b);
    return d(rng);
}

// ==================== HIGH SCORE ====================
int loadHighScore() {
    std::ifstream f("assets/highscore.txt");
    int s = 0;
    if (f) f >> s;
    return s;
}
void saveHighScore(int s) {
    std::ofstream f("assets/highscore.txt");
    if (f) f << s;
}

// ==================== STAR BACKGROUND ====================
struct Star {
    sf::CircleShape shape;
    float layer; // 0=far, 1=near (parallax speed multiplier)
    Star(float x, float y, float r, sf::Color color, float layer_)
        : layer(layer_) {
        shape.setRadius(r);
        shape.setOrigin({r, r});
        shape.setPosition({x, y});
        shape.setFillColor(color);
    }
};

// ==================== GLOW PARTICLE ====================
struct Glow {
    sf::CircleShape shape;
    float life, maxLife;
    sf::Vector2f vel;
    sf::Color baseColor;

    Glow(sf::Vector2f pos, float r, sf::Color col, float life_ = 0.35f,
         sf::Vector2f vel_ = {0.f, 0.f})
        : life(life_), maxLife(life_), vel(vel_), baseColor(col) {
        shape.setRadius(r);
        shape.setOrigin({r, r});
        shape.setPosition(pos);
        shape.setFillColor(col);
    }
    bool update(float dt) {
        life -= dt;
        shape.move(vel * dt);
        float t = std::max(0.f, life / maxLife);
        sf::Color c = baseColor;
        c.a = static_cast<std::uint8_t>(baseColor.a * t);
        shape.setFillColor(c);
        return life > 0.f;
    }
};

// ==================== PARTICLE BURST ====================
// Spawns radial particles at a position (enemy death, hit sparks, etc.)
void spawnBurst(std::vector<Glow>& glows, sf::Vector2f pos,
                sf::Color col, int count = 8, float speed = 90.f,
                float radius = 5.f, float life = 0.45f) {
    for (int i = 0; i < count; ++i) {
        float angle = randf(0.f, 2.f * 3.14159f);
        float spd   = randf(speed * 0.4f, speed);
        sf::Vector2f vel(std::cos(angle) * spd, std::sin(angle) * spd);
        sf::Color c = col;
        c.a = 200;
        glows.emplace_back(pos, randf(radius * 0.4f, radius), c, life, vel);
    }
}

// ==================== PICKUP ====================
enum class PickupType { HEALTH, RAPID_FIRE, SPREAD_SHOT };
struct Pickup {
    sf::CircleShape shape;
    PickupType type;
    float life = 6.f;    // disappears after 6 seconds
    float pulse = 0.f;

    Pickup(sf::Vector2f pos, PickupType t) : type(t) {
        shape.setRadius(10.f);
        shape.setOrigin({10.f, 10.f});
        shape.setPosition(pos);
        switch (t) {
            case PickupType::HEALTH:     shape.setFillColor(sf::Color(80, 255, 80));   break;
            case PickupType::RAPID_FIRE: shape.setFillColor(sf::Color(255, 220, 50));  break;
            case PickupType::SPREAD_SHOT:shape.setFillColor(sf::Color(80, 200, 255));  break;
        }
    }
    bool update(float dt) {
        life -= dt;
        pulse += dt * 4.f;
        float s = 1.f + 0.15f * std::sin(pulse);
        shape.setScale({s, s});
        return life > 0.f;
    }
    bool overlaps(sf::Vector2f p, float r = 14.f) const {
        sf::Vector2f d = shape.getPosition() - p;
        return d.x*d.x + d.y*d.y < (r+10.f)*(r+10.f);
    }
};

// ==================== BULLET ====================
struct Bullet {
    sf::CircleShape s;
    sf::Vector2f vel;
    float life = 1.8f;
    bool dead = false;
    std::vector<Glow> trail;

    Bullet(sf::Vector2f pos, sf::Vector2f dir) {
        s.setRadius(4.f);
        s.setOrigin({4.f, 4.f});
        s.setPosition(pos);
        s.setFillColor(sf::Color(255, 240, 160));
        vel = dir * BULLET_SPEED;
    }
    bool update(float dt) {
        if (dead) return false;
        life -= dt;
        s.move(vel * dt);
        trail.emplace_back(s.getPosition(), 2.5f, sf::Color(255, 210, 110, 170), 0.28f);
        for (auto it = trail.begin(); it != trail.end();) {
            if (!it->update(dt)) it = trail.erase(it);
            else ++it;
        }
        sf::Vector2f p = s.getPosition();
        return life > 0 &&
            p.x > -12 && p.x < WORLD_W + 12 &&
            p.y > -12 && p.y < WORLD_H + 12;
    }
};

// ==================== ENEMIES ====================

// --- Base enemy interface ---
struct Enemy {
    int hp = 1;
    bool dead = false;
    virtual ~Enemy() = default;
    virtual void update(sf::Vector2f target, float dt, float time) = 0;
    virtual void draw(sf::RenderWindow& w) const = 0;
    virtual bool hitBy(sf::Vector2f p) const = 0;
    virtual sf::Vector2f getPosition() const = 0;
    virtual int scoreValue() const = 0;
    virtual sf::Color deathColor() const = 0;
};

// --- Chain Enemy (original) ---
struct ChainEnemy : Enemy {
    std::vector<sf::CircleShape> seg;
    float speed;
    float time = 0.f;

    ChainEnemy(int count, sf::Vector2f start, float spd = 95.f) : speed(spd) {
        hp = count;  // more segments = more HP
        for (int i = 0; i < count; ++i) {
            sf::CircleShape c(6.f);
            c.setOrigin({6.f, 6.f});
            c.setPosition(start - sf::Vector2f((float)i * 12.f, 0.f));
            c.setFillColor(sf::Color(200, 90, 255, 220));
            seg.push_back(c);
        }
    }
    void update(sf::Vector2f target, float dt, float t) override {
        time = t;
        if (seg.empty()) return;
        sf::Vector2f dir = target - seg.front().getPosition();
        float len = std::sqrt(dir.x*dir.x + dir.y*dir.y);
        if (len > 1.f) dir /= len;
        seg.front().move(dir * speed * dt);
        for (size_t i = 1; i < seg.size(); ++i) {
            sf::Vector2f want = seg[i-1].getPosition() - seg[i].getPosition();
            float dist = std::sqrt(want.x*want.x + want.y*want.y);
            if (dist > 12.f) {
                want /= dist;
                seg[i].move(want * (dist - 12.f));
            }
        }
        // Pulse alpha
        for (auto& s : seg) {
            sf::Color c = sf::Color(200, 90, 255, 220);
            c.a = static_cast<uint8_t>(170 + 50 * std::sin(time * 4.f));
            s.setFillColor(c);
        }
    }
    void draw(sf::RenderWindow& w) const override {
        for (auto& s : seg) {
            sf::CircleShape glow(s.getRadius() * 1.8f);
            glow.setOrigin({glow.getRadius(), glow.getRadius()});
            glow.setPosition(s.getPosition());
            sf::Color c = s.getFillColor(); c.a = 90;
            glow.setFillColor(c);
            w.draw(glow, sf::RenderStates(sf::BlendAdd));
        }
        for (auto& s : seg) w.draw(s);
    }
    bool hitBy(sf::Vector2f p) const override {
        for (auto& s : seg) {
            sf::Vector2f d = s.getPosition() - p;
            if (d.x*d.x + d.y*d.y < 12*12) return true;
        }
        return false;
    }
    sf::Vector2f getPosition() const override {
        return seg.empty() ? sf::Vector2f{0,0} : seg.front().getPosition();
    }
    int scoreValue() const override { return 10; }
    sf::Color deathColor() const override { return sf::Color(200, 90, 255); }
};

// --- Tank Enemy: slow, large, takes 5 hits ---
struct TankEnemy : Enemy {
    sf::CircleShape body, inner;
    sf::Vector2f vel;
    float time = 0.f;
    static constexpr float RADIUS = 22.f;

    TankEnemy(sf::Vector2f start) {
        hp = 5;
        body.setRadius(RADIUS);
        body.setOrigin({RADIUS, RADIUS});
        body.setPosition(start);
        body.setFillColor(sf::Color(220, 80, 50, 230));

        inner.setRadius(RADIUS * 0.5f);
        inner.setOrigin({RADIUS*0.5f, RADIUS*0.5f});
        inner.setPosition(start);
        inner.setFillColor(sf::Color(255, 160, 100, 200));
    }
    void update(sf::Vector2f target, float dt, float t) override {
        time = t;
        sf::Vector2f dir = target - body.getPosition();
        float len = std::sqrt(dir.x*dir.x + dir.y*dir.y);
        if (len > 1.f) dir /= len;
        float spd = 55.f;
        body.move(dir * spd * dt);
        inner.setPosition(body.getPosition());

        // Pulse based on HP
        float pulse = 1.f + 0.1f * std::sin(t * 3.f);
        body.setScale({pulse, pulse});
        inner.setScale({pulse, pulse});

        // Color shifts with damage
        int r = 220 + (5 - hp) * 7;
        body.setFillColor(sf::Color((uint8_t)std::min(255,r), 80, 50, 230));
    }
    void draw(sf::RenderWindow& w) const override {
        sf::CircleShape glow(RADIUS * 2.2f);
        glow.setOrigin({RADIUS*2.2f, RADIUS*2.2f});
        glow.setPosition(body.getPosition());
        glow.setFillColor(sf::Color(220, 80, 50, 40));
        w.draw(glow, sf::RenderStates(sf::BlendAdd));
        w.draw(body);
        w.draw(inner);
    }
    bool hitBy(sf::Vector2f p) const override {
        sf::Vector2f d = body.getPosition() - p;
        return d.x*d.x + d.y*d.y < RADIUS*RADIUS;
    }
    sf::Vector2f getPosition() const override { return body.getPosition(); }
    int scoreValue() const override { return 40; }
    sf::Color deathColor() const override { return sf::Color(220, 80, 50); }
};

// --- Shooter Enemy: keeps distance and fires back ---
struct ShooterBullet {
    sf::CircleShape s;
    sf::Vector2f vel;
    float life = 2.5f;
    ShooterBullet(sf::Vector2f pos, sf::Vector2f dir) {
        s.setRadius(5.f);
        s.setOrigin({5.f, 5.f});
        s.setPosition(pos);
        s.setFillColor(sf::Color(255, 80, 80));
        vel = dir * 300.f;
    }
    bool update(float dt) {
        life -= dt;
        s.move(vel * dt);
        return life > 0.f;
    }
};

struct ShooterEnemy : Enemy {
    sf::CircleShape body;
    std::vector<ShooterBullet> eBullets;
    float shootCooldown = 0.f;
    float time = 0.f;
    static constexpr float RADIUS = 13.f;
    static constexpr float PREFERRED_DIST = 180.f;

    ShooterEnemy(sf::Vector2f start) {
        hp = 2;
        body.setRadius(RADIUS);
        body.setOrigin({RADIUS, RADIUS});
        body.setPosition(start);
        body.setFillColor(sf::Color(255, 100, 100, 220));
        shootCooldown = randf(1.0f, 2.5f); // stagger initial shot
    }
    void update(sf::Vector2f target, float dt, float t) override {
        time = t;
        sf::Vector2f dir = target - body.getPosition();
        float dist = std::sqrt(dir.x*dir.x + dir.y*dir.y);
        if (dist > 1.f) dir /= dist;

        // Strafe: maintain preferred distance
        float speed = 80.f;
        if (dist > PREFERRED_DIST + 20.f)
            body.move(dir * speed * dt);
        else if (dist < PREFERRED_DIST - 20.f)
            body.move(-dir * speed * dt);
        else {
            // Orbit sideways
            sf::Vector2f perp(-dir.y, dir.x);
            body.move(perp * speed * dt);
        }

        // Shoot at player
        shootCooldown -= dt;
        if (shootCooldown <= 0.f) {
            shootCooldown = randf(1.6f, 2.8f);
            eBullets.emplace_back(body.getPosition(), dir);
        }
        for (auto it = eBullets.begin(); it != eBullets.end();) {
            if (!it->update(dt)) it = eBullets.erase(it);
            else ++it;
        }

        // Pulse alpha
        sf::Color c = sf::Color(255, 100, 100, 220);
        c.a = static_cast<uint8_t>(180 + 40 * std::sin(t * 5.f));
        body.setFillColor(c);
    }
    void draw(sf::RenderWindow& w) const override {
        sf::CircleShape glow(RADIUS * 2.f);
        glow.setOrigin({RADIUS*2.f, RADIUS*2.f});
        glow.setPosition(body.getPosition());
        glow.setFillColor(sf::Color(255, 100, 100, 40));
        w.draw(glow, sf::RenderStates(sf::BlendAdd));
        w.draw(body);
        for (auto& b : eBullets) w.draw(b.s);
    }
    bool hitBy(sf::Vector2f p) const override {
        sf::Vector2f d = body.getPosition() - p;
        return d.x*d.x + d.y*d.y < RADIUS*RADIUS;
    }
    sf::Vector2f getPosition() const override { return body.getPosition(); }
    int scoreValue() const override { return 25; }
    sf::Color deathColor() const override { return sf::Color(255, 100, 100); }

    // Check if shooter's bullets hit the player
    bool playerHitByBullets(sf::Vector2f playerPos, float playerR = 10.f) {
        for (auto& b : eBullets) {
            sf::Vector2f d = b.s.getPosition() - playerPos;
            if (d.x*d.x + d.y*d.y < (playerR + 5.f)*(playerR + 5.f))
                return true;
        }
        return false;
    }
};

// --- Splitter Enemy: on death splits into 2 fast small ones ---
struct SplitterEnemy : Enemy {
    sf::CircleShape body;
    float speed;
    bool isChild;
    float time = 0.f;
    float RADIUS;

    SplitterEnemy(sf::Vector2f start, bool child = false, float spd = 110.f)
        : speed(spd), isChild(child) {
        RADIUS = child ? 7.f : 14.f;
        hp = child ? 1 : 2;
        body.setRadius(RADIUS);
        body.setOrigin({RADIUS, RADIUS});
        body.setPosition(start);
        body.setFillColor(child ? sf::Color(100, 255, 200, 220)
                                : sf::Color(50, 220, 160, 220));
    }
    void update(sf::Vector2f target, float dt, float t) override {
        time = t;
        sf::Vector2f dir = target - body.getPosition();
        float len = std::sqrt(dir.x*dir.x + dir.y*dir.y);
        if (len > 1.f) dir /= len;
        body.move(dir * speed * dt);

        float pulse = 1.f + 0.12f * std::sin(t * 6.f);
        body.setScale({pulse, pulse});
    }
    void draw(sf::RenderWindow& w) const override {
        sf::CircleShape glow(RADIUS * 2.f);
        glow.setOrigin({RADIUS*2.f, RADIUS*2.f});
        glow.setPosition(body.getPosition());
        glow.setFillColor(sf::Color(50, 220, 160, 45));
        w.draw(glow, sf::RenderStates(sf::BlendAdd));
        w.draw(body);
    }
    bool hitBy(sf::Vector2f p) const override {
        sf::Vector2f d = body.getPosition() - p;
        return d.x*d.x + d.y*d.y < RADIUS*RADIUS;
    }
    sf::Vector2f getPosition() const override { return body.getPosition(); }
    int scoreValue() const override { return isChild ? 5 : 20; }
    sf::Color deathColor() const override { return sf::Color(50, 220, 160); }
};

// ==================== WAVE ANNOUNCE ====================
struct WaveAnnounce {
    sf::Text text;
    float life = 0.f;

    WaveAnnounce(sf::Font& font) : text(font, "", 56) {
        text.setFillColor(sf::Color(255, 220, 80));
        text.setOutlineColor(sf::Color(100, 60, 0, 180));
        text.setOutlineThickness(3.f);
    }
    void trigger(int wave, sf::Font& font) {
        text.setString("WAVE " + std::to_string(wave) + "!");
        auto bounds = text.getLocalBounds();
        text.setOrigin({bounds.position.x + bounds.size.x / 2.f,
                        bounds.position.y + bounds.size.y / 2.f});
        text.setPosition({WINDOW_W / 2.f, WINDOW_H / 2.f - 60.f});
        life = 2.2f;
        (void)font;
    }
    bool update(float dt) {
        if (life <= 0.f) return false;
        life -= dt;
        float t = life / 2.2f;
        float alpha = t > 0.8f ? 255.f
                    : t < 0.2f ? 255.f * (t / 0.2f)
                    : 255.f;
        float scale = 1.f + 0.08f * std::sin(life * 8.f);
        text.setScale({scale, scale});
        sf::Color c = text.getFillColor();
        c.a = static_cast<uint8_t>(alpha);
        text.setFillColor(c);
        return life > 0.f;
    }
    void draw(sf::RenderWindow& w) const {
        if (life > 0.f) w.draw(text);
    }
};

// ==================== COMBO SYSTEM ====================
struct Combo {
    int count = 0;
    int multiplier = 1;
    float timer = 0.f;
    static constexpr float COMBO_WINDOW = 2.5f;

    void hit() {
        count++;
        timer = COMBO_WINDOW;
        multiplier = std::min(8, 1 + count / 3);
    }
    void reset() { count = 0; multiplier = 1; timer = 0.f; }
    void update(float dt) {
        if (timer > 0.f) {
            timer -= dt;
            if (timer <= 0.f) reset();
        }
    }
    int apply(int baseScore) const { return baseScore * multiplier; }
};

// ==================== MAIN ====================
int main() {
    sf::RenderWindow window(
        sf::VideoMode({(unsigned)WINDOW_W, (unsigned)WINDOW_H}),
        "Yawnoc - Enhanced");
    window.setFramerateLimit(60);

    sf::View gameView(sf::FloatRect({0.f,0.f},{(float)WINDOW_W,(float)WINDOW_H}));
    window.setView(gameView);

    sf::Font font;
    [[maybe_unused]] bool loaded = font.openFromFile("assets/font.ttf");

    // ---- State ----
    GameState state = GameState::MENU;
    int score = 0, highScore = loadHighScore(), health = MAX_HEALTH, wave = 1;
    int nextWaveScore = 100;
    float shootTimer = 0.f, enemyTimer = 0.f, enemySpawnRate = 1.6f;
    float shakeTimer = 0.f, shakeIntensity = 0.f;
    float damageFlash = 0.f;
    float globalTime = 0.f;
    // Power-up timers
    float rapidFireTimer = 0.f;
    float spreadShotTimer = 0.f;
    bool spreadShot = false;
    bool rapidFire = false;

    // ---- Player ----
    sf::Clock clock;
    sf::ConvexShape player(3);
    player.setPoint(0, {0.f, -14.f});
    player.setPoint(1, {10.f, 10.f});
    player.setPoint(2, {-10.f, 10.f});
    player.setOrigin({0.f, 0.f});
    player.setFillColor(sf::Color(120, 255, 140));
    player.setPosition({WINDOW_W / 2.f, WINDOW_H / 2.f});
    float playerAngle = 0.f;

    // ---- Background stars (3 layers for parallax) ----
    std::vector<Star> stars;
    for (int i = 0; i < 450; ++i) {
        float layer = (i < 150) ? 0.0f : (i < 300) ? 0.4f : 0.8f;
        float r = (layer < 0.3f) ? randf(0.4f, 0.9f) : (layer < 0.6f) ? randf(0.8f, 1.3f) : randf(1.1f, 2.0f);
        sf::Color c(255, 255, 255, (uint8_t)(60 + layer * 120));
        stars.emplace_back(randf(0,WORLD_W), randf(0,WORLD_H), r, c, layer);
    }

    // ---- World objects ----
    std::vector<Bullet> bullets;
    std::vector<std::unique_ptr<Enemy>> enemies;
    std::vector<Glow> worldGlows;   // world-space particles
    std::vector<Pickup> pickups;

    // ---- UI shapes ----
    sf::RectangleShape hpBack({200.f, 16.f});
    hpBack.setPosition({(float)WINDOW_W - 220.f, 10.f});
    hpBack.setFillColor({40, 40, 40});

    sf::RectangleShape hpBar({200.f, 16.f});
    hpBar.setPosition({(float)WINDOW_W - 220.f, 10.f});
    hpBar.setFillColor(sf::Color::Green);

    // Power-up indicators
    sf::RectangleShape rapidFireBar({0.f, 8.f});
    rapidFireBar.setPosition({(float)WINDOW_W - 220.f, 30.f});
    rapidFireBar.setFillColor(sf::Color(255, 220, 50));

    sf::RectangleShape spreadShotBar({0.f, 8.f});
    spreadShotBar.setPosition({(float)WINDOW_W - 220.f, 42.f});
    spreadShotBar.setFillColor(sf::Color(80, 200, 255));

    // ---- Texts ----
    sf::Text title(font, "YAWNOC", 76);
    {
        auto b = title.getLocalBounds();
        title.setOrigin({b.position.x + b.size.x/2.f, b.position.y + b.size.y/2.f});
        title.setPosition({WINDOW_W/2.f, 120.f});
    }
    sf::Text startBtn(font, "[ Start Game ]", 32);
    {
        auto b = startBtn.getLocalBounds();
        startBtn.setOrigin({b.position.x + b.size.x/2.f, b.position.y + b.size.y/2.f});
        startBtn.setPosition({WINDOW_W/2.f, 260.f});
    }
    sf::Text exitBtn(font, "[ Exit ]", 32);
    {
        auto b = exitBtn.getLocalBounds();
        exitBtn.setOrigin({b.position.x + b.size.x/2.f, b.position.y + b.size.y/2.f});
        exitBtn.setPosition({WINDOW_W/2.f, 320.f});
    }

    sf::Text scoreText(font, "Score: 0", 18);
    scoreText.setPosition({10.f, 10.f});
    sf::Text hsText(font, "High: 0", 18);
    hsText.setPosition({10.f, 36.f});
    sf::Text waveText(font, "Wave: 1", 18);
    waveText.setPosition({10.f, 62.f});

    sf::Text comboText(font, "", 22);
    comboText.setFillColor(sf::Color(255, 220, 80));
    comboText.setPosition({10.f, 90.f});

    sf::Text gameOverText(font, "GAME OVER", 64);
    gameOverText.setFillColor(sf::Color::Red);
    {
        auto b = gameOverText.getLocalBounds();
        gameOverText.setOrigin({b.position.x + b.size.x/2.f, b.position.y + b.size.y/2.f});
        gameOverText.setPosition({WINDOW_W/2.f, 200.f});
    }
    sf::Text restartText(font, "Press R to Restart  |  ESC to Exit", 26);
    {
        auto b = restartText.getLocalBounds();
        restartText.setOrigin({b.position.x + b.size.x/2.f, b.position.y + b.size.y/2.f});
        restartText.setPosition({WINDOW_W/2.f, 295.f});
    }
    sf::Text finalScoreText(font, "", 30);
    finalScoreText.setFillColor(sf::Color(200,200,255));
    {
        auto b = finalScoreText.getLocalBounds();
        finalScoreText.setOrigin({b.position.x + b.size.x/2.f, b.position.y + b.size.y/2.f});
        finalScoreText.setPosition({WINDOW_W/2.f, 250.f});
    }

    // ---- Wave announce ----
    WaveAnnounce waveAnnounce(font);

    // ---- Combo ----
    Combo combo;

    // ---- Muzzle flash ----
    Glow muzzleFlash({0,0}, 8.f, sf::Color(255, 255, 180, 0), 0.08f);
    bool showMuzzle = false;

    // ---- Camera ----
    sf::Vector2f camCenter((float)WINDOW_W/2.f, (float)WINDOW_H/2.f);

    // ==================== GAME LOOP ====================
    while (window.isOpen()) {
        float dt = clock.restart().asSeconds();
        dt = std::min(dt, 0.05f); // clamp to avoid spiral of death
        globalTime += dt;
        shootTimer += dt;
        enemyTimer += dt;
        if (shakeTimer > 0.f) shakeTimer -= dt;
        if (damageFlash > 0.f) damageFlash -= dt;
        if (rapidFireTimer > 0.f) { rapidFireTimer -= dt; rapidFire = true; }
        else rapidFire = false;
        if (spreadShotTimer > 0.f) { spreadShotTimer -= dt; spreadShot = true; }
        else spreadShot = false;

        // ---- Events ----
        while (auto ev = window.pollEvent()) {
            if (ev->is<sf::Event::Closed>()) window.close();
            if (auto kp = ev->getIf<sf::Event::KeyPressed>()) {
                if (kp->scancode == sf::Keyboard::Scan::Escape) window.close();
                if (state == GameState::GAME_OVER && kp->scancode == sf::Keyboard::Scan::R)
                    state = GameState::MENU;
            }
            if (auto mb = ev->getIf<sf::Event::MouseButtonPressed>()) {
                sf::Vector2f world = window.mapPixelToCoords(
                    {mb->position.x, mb->position.y}, window.getDefaultView());
                if (state == GameState::MENU) {
                    if (startBtn.getGlobalBounds().contains(world)) {
                        state = GameState::PLAYING;
                        score = 0; health = MAX_HEALTH; wave = 1;
                        nextWaveScore = 100;
                        enemySpawnRate = 1.6f;
                        bullets.clear(); enemies.clear();
                        worldGlows.clear(); pickups.clear();
                        combo.reset();
                        rapidFireTimer = spreadShotTimer = 0.f;
                        player.setPosition({WINDOW_W/2.f, WINDOW_H/2.f});
                        camCenter = player.getPosition();
                        waveAnnounce.trigger(1, font);
                    }
                    if (exitBtn.getGlobalBounds().contains(world)) window.close();
                }
            }
        }

        // ==================== GAME OVER SCREEN ====================
        if (state == GameState::GAME_OVER) {
            window.clear(sf::Color(18, 10, 28));
            window.draw(gameOverText);
            window.draw(finalScoreText);
            window.draw(restartText);
            window.display();
            continue;
        }

        // ==================== PLAYING ====================
        if (state == GameState::PLAYING) {
            // ---- Player movement ----
            sf::Vector2f move{0,0};
            if (sf::Keyboard::isKeyPressed(sf::Keyboard::Scan::A)) move.x -= 1;
            if (sf::Keyboard::isKeyPressed(sf::Keyboard::Scan::D)) move.x += 1;
            if (sf::Keyboard::isKeyPressed(sf::Keyboard::Scan::W)) move.y -= 1;
            if (sf::Keyboard::isKeyPressed(sf::Keyboard::Scan::S)) move.y += 1;
            float mlen = std::sqrt(move.x*move.x + move.y*move.y);
            if (mlen > 1.f) move /= mlen;
            player.move(move * PLAYER_SPEED * dt);

            sf::Vector2f pos = player.getPosition();
            float rP = 12.f;
            pos.x = std::clamp(pos.x, rP, (float)WORLD_W - rP);
            pos.y = std::clamp(pos.y, rP, (float)WORLD_H - rP);
            player.setPosition(pos);

            // ---- Player rotation toward mouse ----
            sf::Vector2f mousePos = window.mapPixelToCoords(
                sf::Mouse::getPosition(window), gameView);
            sf::Vector2f aim = mousePos - player.getPosition();
            float aimLen = std::sqrt(aim.x*aim.x + aim.y*aim.y);
            if (aimLen > 0.001f) {
                playerAngle = std::atan2(aim.y, aim.x) * 180.f / 3.14159f + 90.f;
                player.setRotation(sf::degrees(playerAngle));
            }

            // ---- Shooting ----
            float currentFireDelay = rapidFire ? FIRE_DELAY * 0.45f : FIRE_DELAY;
            if (sf::Mouse::isButtonPressed(sf::Mouse::Button::Left) && shootTimer > currentFireDelay) {
                shootTimer = 0.f;
                sf::Vector2f aimDir = aimLen > 0.001f ? aim / aimLen : sf::Vector2f{0,-1};

                // Muzzle flash
                showMuzzle = true;
                sf::Vector2f muzzlePos = player.getPosition() + aimDir * 16.f;
                muzzleFlash = Glow(muzzlePos, 8.f, sf::Color(255, 255, 180, 200), 0.09f);

                bullets.emplace_back(player.getPosition(), aimDir);
                if (spreadShot) {
                    // Two extra angled bullets
                    float ang = std::atan2(aimDir.y, aimDir.x);
                    float spread = 0.22f; // radians
                    sf::Vector2f d1(std::cos(ang - spread), std::sin(ang - spread));
                    sf::Vector2f d2(std::cos(ang + spread), std::sin(ang + spread));
                    bullets.emplace_back(player.getPosition(), d1);
                    bullets.emplace_back(player.getPosition(), d2);
                }
            }

            // ---- Update bullets ----
            for (auto it = bullets.begin(); it != bullets.end();) {
                if (!it->update(dt) || it->dead) it = bullets.erase(it);
                else ++it;
            }

            // ---- Spawn enemies ----
            if (enemyTimer > enemySpawnRate) {
                enemyTimer = 0.f;
                // Spawn at edge of screen
                sf::Vector2f spawnPos;
                int edge = randi(0, 3);
                if (edge == 0) spawnPos = {randf(0, WORLD_W), -20.f};
                else if (edge == 1) spawnPos = {randf(0, WORLD_W), (float)WORLD_H + 20.f};
                else if (edge == 2) spawnPos = {-20.f, randf(0, WORLD_H)};
                else spawnPos = {(float)WORLD_W + 20.f, randf(0, WORLD_H)};

                // Wave-weighted random enemy type
                int typeRoll = randi(0, 9);
                if (wave <= 1 || typeRoll < 5) {
                    enemies.push_back(std::make_unique<ChainEnemy>(5 + wave, spawnPos));
                } else if (typeRoll < 7) {
                    enemies.push_back(std::make_unique<TankEnemy>(spawnPos));
                } else if (typeRoll < 9) {
                    enemies.push_back(std::make_unique<ShooterEnemy>(spawnPos));
                } else {
                    enemies.push_back(std::make_unique<SplitterEnemy>(spawnPos));
                }
            }

            // ---- Update enemies + hit detection ----
            std::vector<std::unique_ptr<Enemy>> newEnemies;

            for (auto eit = enemies.begin(); eit != enemies.end();) {
                (*eit)->update(player.getPosition(), dt, globalTime);

                bool killed = false;

                // Bullet hits enemy
                for (auto& b : bullets) {
                    if (!b.dead && (*eit)->hitBy(b.s.getPosition())) {
                        b.dead = true;
                        (*eit)->hp--;
                        // Spark at impact
                        spawnBurst(worldGlows, b.s.getPosition(),
                                   (*eit)->deathColor(), 5, 60.f, 3.5f, 0.3f);
                        if ((*eit)->hp <= 0) {
                            killed = true;
                            int pts = combo.apply((*eit)->scoreValue());
                            score += pts;
                            combo.hit();
                            shakeTimer = 0.18f; shakeIntensity = 6.f;

                            // Death burst
                            spawnBurst(worldGlows, (*eit)->getPosition(),
                                       (*eit)->deathColor(), 14, 120.f, 7.f, 0.55f);

                            // Splitter logic
                            if (auto* sp = dynamic_cast<SplitterEnemy*>(eit->get())) {
                                if (!sp->isChild) {
                                    for (int k = 0; k < 2; ++k) {
                                        sf::Vector2f off(randf(-20,20), randf(-20,20));
                                        newEnemies.push_back(
                                            std::make_unique<SplitterEnemy>(
                                                sp->getPosition() + off, true, 160.f));
                                    }
                                }
                            }

                            // Random pickup drop (25%)
                            if (randi(0, 3) == 0) {
                                PickupType pt = (PickupType)randi(0, 2);
                                pickups.emplace_back((*eit)->getPosition(), pt);
                            }
                        }
                        break;
                    }
                }

                // Shooter enemy fires at player
                if (!killed) {
                    if (auto* sh = dynamic_cast<ShooterEnemy*>(eit->get())) {
                        if (sh->playerHitByBullets(player.getPosition())) {
                            health -= 8;
                            damageFlash = 0.35f;
                            shakeTimer = 0.3f; shakeIntensity = 10.f;
                            combo.reset();
                            sh->eBullets.clear();
                        }
                    }
                }

                // Enemy touches player
                if (!killed && (*eit)->hitBy(player.getPosition())) {
                    health -= 12;
                    killed = true;
                    damageFlash = 0.4f;
                    shakeTimer = 0.4f; shakeIntensity = 12.f;
                    combo.reset();
                    spawnBurst(worldGlows, player.getPosition(),
                               sf::Color(255,100,100), 10, 80.f, 5.f, 0.4f);
                }

                if (killed) eit = enemies.erase(eit);
                else ++eit;
            }
            for (auto& ne : newEnemies) enemies.push_back(std::move(ne));

            // ---- Update world glows ----
            for (auto it = worldGlows.begin(); it != worldGlows.end();) {
                if (!it->update(dt)) it = worldGlows.erase(it);
                else ++it;
            }

            // ---- Update pickups ----
            for (auto it = pickups.begin(); it != pickups.end();) {
                if (!it->update(dt)) { it = pickups.erase(it); continue; }
                if (it->overlaps(player.getPosition())) {
                    switch(it->type) {
                        case PickupType::HEALTH:
                            health = std::min(MAX_HEALTH, health + 20);
                            spawnBurst(worldGlows, player.getPosition(),
                                       sf::Color(80,255,80), 8, 60.f, 4.f, 0.4f);
                            break;
                        case PickupType::RAPID_FIRE:
                            rapidFireTimer = 5.f;
                            spawnBurst(worldGlows, player.getPosition(),
                                       sf::Color(255,220,50), 8, 60.f, 4.f, 0.4f);
                            break;
                        case PickupType::SPREAD_SHOT:
                            spreadShotTimer = 6.f;
                            spawnBurst(worldGlows, player.getPosition(),
                                       sf::Color(80,200,255), 8, 60.f, 4.f, 0.4f);
                            break;
                    }
                    it = pickups.erase(it);
                } else ++it;
            }

            // ---- Wave progression ----
            if (score >= nextWaveScore) {
                wave++;
                nextWaveScore += 100 + wave * 30;
                enemySpawnRate = std::max(0.55f, enemySpawnRate - 0.12f);
                waveAnnounce.trigger(wave, font);
            }

            // ---- Combo update ----
            combo.update(dt);

            // ---- Game over check ----
            if (health <= 0) {
                state = GameState::GAME_OVER;
                if (score > highScore) {
                    highScore = score;
                    saveHighScore(highScore);
                }
                finalScoreText.setString("Score: " + std::to_string(score) +
                                         "   Best: " + std::to_string(highScore));
                auto b = finalScoreText.getLocalBounds();
                finalScoreText.setOrigin({b.position.x + b.size.x/2.f, b.position.y + b.size.y/2.f});
                continue;
            }

            // ---- Camera ----
            camCenter += (player.getPosition() - camCenter) * 5.f * dt;
            if (shakeTimer > 0.f) {
                camCenter += sf::Vector2f(randf(-1,1) * shakeIntensity,
                                         randf(-1,1) * shakeIntensity);
            }
            camCenter.x = std::clamp(camCenter.x, (float)WINDOW_W/2.f, (float)WORLD_W - WINDOW_W/2.f);
            camCenter.y = std::clamp(camCenter.y, (float)WINDOW_H/2.f, (float)WORLD_H - WINDOW_H/2.f);
            gameView.setCenter(camCenter);
            window.setView(gameView);

            // ---- HP bar ----
            float hpRatio = std::max(0.f, (float)health / MAX_HEALTH);
            hpBar.setSize({200.f * hpRatio, 16.f});
            hpBar.setFillColor(hpRatio > 0.5f ? sf::Color::Green
                             : hpRatio > 0.25f ? sf::Color::Yellow : sf::Color::Red);

            // ---- Power-up bars ----
            rapidFireBar.setSize({200.f * std::max(0.f, rapidFireTimer / 5.f), 8.f});
            spreadShotBar.setSize({200.f * std::max(0.f, spreadShotTimer / 6.f), 8.f});

            // ---- UI strings ----
            scoreText.setString("Score: " + std::to_string(score));
            hsText.setString("Best:  " + std::to_string(highScore));
            waveText.setString("Wave:  " + std::to_string(wave));
            if (combo.multiplier > 1)
                comboText.setString("x" + std::to_string(combo.multiplier) + " COMBO!");
            else comboText.setString("");

            // ==================== DRAW ====================
            window.clear(sf::Color(10, 8, 20));

            // --- Background stars (parallax: fixed to screen) ---
            window.setView(window.getDefaultView());
            for (auto& star : stars) {
                if (star.layer < 0.1f) {
                    window.draw(star.shape); // far layer: no parallax
                }
            }
            // Near layers: slight offset based on cam deviation
            sf::Vector2f camOffset = camCenter - sf::Vector2f(WINDOW_W/2.f, WINDOW_H/2.f);
            for (auto& star : stars) {
                if (star.layer >= 0.1f) {
                    sf::CircleShape s = star.shape;
                    sf::Vector2f p = star.shape.getPosition();
                    p -= camOffset * star.layer * 0.08f;
                    // Wrap
                    p.x = std::fmod(p.x + WORLD_W, (float)WORLD_W);
                    p.y = std::fmod(p.y + WORLD_H, (float)WORLD_H);
                    s.setPosition(p);
                    window.draw(s);
                }
            }
            window.setView(gameView);

            // --- World particles ---
            for (auto& g : worldGlows)
                window.draw(g.shape, sf::RenderStates(sf::BlendAdd));

            // --- Pickups ---
            for (auto& p : pickups) {
                sf::CircleShape glow(16.f);
                glow.setOrigin({16.f,16.f});
                glow.setPosition(p.shape.getPosition());
                sf::Color gc = p.shape.getFillColor(); gc.a = 60;
                glow.setFillColor(gc);
                window.draw(glow, sf::RenderStates(sf::BlendAdd));
                window.draw(p.shape);
            }

            // --- Enemies ---
            for (auto& e : enemies) e->draw(window);

            // --- Bullets ---
            for (auto& b : bullets) {
                for (auto& g : b.trail)
                    window.draw(g.shape, sf::RenderStates(sf::BlendAdd));
                window.draw(b.s);
            }

            // --- Muzzle flash ---
            if (showMuzzle) {
                if (muzzleFlash.life > 0.f) {
                    window.draw(muzzleFlash.shape, sf::RenderStates(sf::BlendAdd));
                    muzzleFlash.update(dt);
                } else showMuzzle = false;
            }

            // --- Player ---
            window.draw(player);

            // --- Wave announce (world space, centered) ---
            window.setView(window.getDefaultView());
            waveAnnounce.update(dt);
            waveAnnounce.draw(window);

            // --- HUD ---
            window.draw(hpBack);
            window.draw(hpBar);
            if (rapidFire)  window.draw(rapidFireBar);
            if (spreadShot) window.draw(spreadShotBar);
            window.draw(scoreText);
            window.draw(hsText);
            window.draw(waveText);
            window.draw(comboText);

        } else if (state == GameState::MENU) {
            // ==================== MENU ====================
            window.setView(window.getDefaultView());
            window.clear(sf::Color(10, 8, 20));

            // Animate stars on menu
            for (auto& star : stars) window.draw(star.shape);

            // Title pulse
            float s = 1.f + 0.04f * std::sin(globalTime * 2.f);
            title.setScale({s, s});
            title.setFillColor(sf::Color(
                (uint8_t)(180 + 75 * std::sin(globalTime * 1.2f)),
                (uint8_t)(180 + 75 * std::sin(globalTime * 1.5f + 1.f)),
                255));
            window.draw(title);

            // Hover highlight
            sf::Vector2f mpos = window.mapPixelToCoords(sf::Mouse::getPosition(window));
            startBtn.setFillColor(startBtn.getGlobalBounds().contains(mpos) ?
                sf::Color(180,255,180) : sf::Color::White);
            exitBtn.setFillColor(exitBtn.getGlobalBounds().contains(mpos) ?
                sf::Color(255,180,180) : sf::Color::White);

            window.draw(startBtn);
            window.draw(exitBtn);
            hsText.setString("Best: " + std::to_string(highScore));
            hsText.setPosition({WINDOW_W/2.f - 60.f, 380.f});
            window.draw(hsText);
        }

        // ---- Damage flash overlay (always on top) ----
        if (damageFlash > 0.f) {
            float alpha = 255.f * (damageFlash / 0.4f);
            sf::RectangleShape flash(sf::Vector2f(WINDOW_W, WINDOW_H));
            flash.setFillColor(sf::Color(255, 0, 0, (uint8_t)(alpha * 0.25f)));
            flash.setOutlineColor(sf::Color(255, 0, 0, (uint8_t)std::min(255.f, alpha)));
            flash.setOutlineThickness(22.f);
            window.setView(window.getDefaultView());
            window.draw(flash);
        }

        window.display();
    }

    return 0;
}
