#include <SFML/Graphics.hpp>
#include <cmath>
#include <vector>
#include <random>
#include <fstream>
#include <string>
#include <algorithm>
#include <cstdint>

constexpr int WINDOW_W = 960;
constexpr int WINDOW_H = 540;
constexpr int WORLD_W = 1920;  
constexpr int WORLD_H = 1080;
constexpr float PLAYER_SPEED = 240.f;
constexpr float BULLET_SPEED = 750.f;
constexpr float FIRE_DELAY = 0.10f;
constexpr int MAX_HEALTH = 100;

enum class GameState { MENU, PLAYING, GAME_OVER };

float randf(float a, float b) {
    static std::mt19937 rng(std::random_device{}());
    std::uniform_real_distribution<float> d(a, b);
    return d(rng);
}

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

// -------------------- BACKGROUND --------------------
struct Star {
    sf::CircleShape shape;
    Star(float x, float y, float r, sf::Color color) {
        shape.setRadius(r);
        shape.setOrigin({r, r});
        shape.setPosition({x, y});
        shape.setFillColor(color);
    }
};


// -------------------- GLOW --------------------
struct Glow {
    sf::CircleShape shape;
    float life;
    sf::Vector2f vel;
    Glow(sf::Vector2f pos, float r, sf::Color col, float life_ = 0.35f)
        : life(life_), vel(0.f, 0.f) {
        shape.setRadius(r);
        shape.setOrigin({r, r});
        shape.setPosition(pos);
        shape.setFillColor(col);
    }
    bool update(float dt) {
        life -= dt;
        auto c = shape.getFillColor();
        c.a = static_cast<std::uint8_t>(255 * std::max(0.f, life / 0.35f));
        shape.setFillColor(c);
        return life > 0.f;
    }
};

// -------------------- BULLET --------------------
struct Bullet {
    sf::CircleShape s;
    sf::Vector2f vel;
    float life = 1.8f;
    std::vector<Glow> trail;
    Bullet(sf::Vector2f pos, sf::Vector2f dir) {
        s.setRadius(4.f);
        s.setOrigin({4.f, 4.f});
        s.setPosition(pos);
        s.setFillColor(sf::Color(255, 240, 160));
        vel = dir * BULLET_SPEED;
    }
    bool update(float dt) {
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

// -------------------- ENEMY --------------------
struct ChainEnemy {
    std::vector<sf::CircleShape> seg;
    float speed = 95.f;
    ChainEnemy(int count, sf::Vector2f start) {
        for (int i = 0; i < count; ++i) {
            sf::CircleShape c(6.f);
            c.setOrigin({6.f, 6.f});
            c.setPosition(start - sf::Vector2f(i * 12.f, 0.f));
            c.setFillColor(sf::Color(200, 90, 255, 220));
            seg.push_back(c);
        }
    }
    void update(sf::Vector2f target, float dt) {
        if (seg.empty()) return;
        sf::Vector2f dir = target - seg.front().getPosition();
        float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
        if (len > 1.f) dir /= len;
        seg.front().move(dir * speed * dt);
        for (size_t i = 1; i < seg.size(); ++i) {
            sf::Vector2f want = seg[i - 1].getPosition() - seg[i].getPosition();
            float dist = std::sqrt(want.x * want.x + want.y * want.y);
            if (dist > 12.f) {
                want /= dist;
                seg[i].move(want * (dist - 12.f));
            }
        }
    }
    void draw(sf::RenderWindow& w) const {
        for (auto& s : seg) {
            sf::CircleShape glow(s.getRadius() * 1.8f);
            glow.setOrigin({glow.getRadius(), glow.getRadius()});
            glow.setPosition(s.getPosition());
            sf::Color c = s.getFillColor();
            c.a = 90;
            w.draw(glow, sf::RenderStates(sf::BlendAdd));
        }
        for (auto& s : seg) w.draw(s);
    }
    bool hitBy(sf::Vector2f p) const {
        for (auto& s : seg) {
            sf::Vector2f d = s.getPosition() - p;
            if (d.x * d.x + d.y * d.y < 12 * 12) return true;
        }
        return false;
    }
};

// -------------------- MAIN --------------------
int main() {
    sf::RenderWindow window(sf::VideoMode({(unsigned)WINDOW_W, (unsigned)WINDOW_H}), "Yawnoc - SFML 3.0 Final");



    window.setFramerateLimit(60);

    sf::View view(sf::FloatRect({0.f, 0.f}, {(float)WINDOW_W, (float)WINDOW_H}));
    window.setView(view);

    sf::Font font;
    [[maybe_unused]] bool loaded = font.openFromFile("assets/font.ttf");

    GameState state = GameState::MENU;
    int score = 0, highScore = loadHighScore(), health = MAX_HEALTH, wave = 1;
    int nextWaveScore = 100;
    float shootTimer = 0.f, enemyTimer = 0.f, enemySpawnRate = 1.6f;
    float shakeTimer = 0.f, shakeIntensity = 0.f;
float damageFlash = 0.f;  // time left for red border flash


    sf::Clock clock;
    sf::CircleShape player(10.f);
    player.setOrigin({10.f, 10.f});
    player.setFillColor(sf::Color(120, 255, 140));
    player.setPosition({WINDOW_W / 2.f, WINDOW_H / 2.f});

// Generate background stars
std::vector<Star> stars;
const int STAR_COUNT = 400;  // increase for denser background
for (int i = 0; i < STAR_COUNT; ++i) {
    float x = randf(0, WORLD_W);
    float y = randf(0, WORLD_H);
    float r = randf(0.5f, 1.5f);
    sf::Color c(255, 255, 255, rand() % 120 + 80); // faint white dots
    stars.emplace_back(x, y, r, c);
}


    std::vector<Bullet> bullets;
    std::vector<ChainEnemy> enemies;

    sf::RectangleShape hpBack({200.f, 16.f});
    hpBack.setPosition(sf::Vector2f{(float)WINDOW_W - 220.f, 10.f});
    hpBack.setFillColor({40, 40, 40});

    sf::RectangleShape hpBar({200.f, 16.f});
    hpBar.setPosition(sf::Vector2f{(float)WINDOW_W - 220.f, 10.f});

    hpBar.setFillColor(sf::Color::Green);

    sf::Text title(font, "YAWNOC", 76);
    title.setPosition(sf::Vector2f{WINDOW_W / 2.f - 200.f, 80.f});
    sf::Text startBtn(font, "Start Game", 30);
    startBtn.setPosition(sf::Vector2f{WINDOW_W / 2.f - 100.f, 260.f});
    sf::Text exitBtn(font, "Exit", 30);
    exitBtn.setPosition(sf::Vector2f{WINDOW_W / 2.f - 25.f, 320.f});

    sf::Text scoreText(font, "Score: 0", 18);
    scoreText.setPosition(sf::Vector2f{10.f, 10.f});
    sf::Text hsText(font, "High: 0", 18);
    hsText.setPosition(sf::Vector2f{10.f, 36.f});
    sf::Text waveText(font, "Wave: 1", 18);
    waveText.setPosition(sf::Vector2f{10.f, 62.f});

    sf::Text gameOverText(font, "GAME OVER", 64);
    gameOverText.setFillColor(sf::Color::Red);
    gameOverText.setPosition(sf::Vector2f{WINDOW_W / 2.f - 200.f, 200.f});

    sf::Text restartText(font, "Press R to Restart or ESC to Exit", 28);
    restartText.setPosition(sf::Vector2f{WINDOW_W / 2.f - 220.f, 300.f});

    while (window.isOpen()) {
        float dt = clock.restart().asSeconds();
        shootTimer += dt;
        enemyTimer += dt;
        shakeTimer -= dt;
        if (shakeTimer < 0.f) shakeTimer = 0.f;

        while (auto ev = window.pollEvent()) {
            if (ev->is<sf::Event::Closed>()) window.close();
            if (ev->is<sf::Event::KeyPressed>()) {
                auto kp = ev->getIf<sf::Event::KeyPressed>();
                if (kp->scancode == sf::Keyboard::Scan::Escape) window.close();
                if (state == GameState::GAME_OVER && kp->scancode == sf::Keyboard::Scan::R)
                    state = GameState::MENU;
            }
            if (ev->is<sf::Event::MouseButtonPressed>()) {
                auto mb = ev->getIf<sf::Event::MouseButtonPressed>();
                if (mb) {
                    sf::Vector2f world = window.mapPixelToCoords(sf::Vector2i{mb->position.x, mb->position.y});
                    if (state == GameState::MENU) {
                        if (startBtn.getGlobalBounds().contains(world)) {
                            state = GameState::PLAYING;
                            score = 0;
                            health = MAX_HEALTH;
                            wave = 1;
                            nextWaveScore = 100;
                            bullets.clear();
                            enemies.clear();
                            player.setPosition(sf::Vector2f{WINDOW_W / 2.f, WINDOW_H / 2.f});
                        }
                        if (exitBtn.getGlobalBounds().contains(world)) window.close();
                    }
                }
            }
        }

        if (state == GameState::GAME_OVER) {
            window.clear(sf::Color(30, 30, 50));
            window.draw(gameOverText);
            window.draw(restartText);
            window.display();
            continue;
        }

        if (state == GameState::PLAYING) {
            sf::Vector2f move{0, 0};
            if (sf::Keyboard::isKeyPressed(sf::Keyboard::Scan::A)) move.x -= 1;
            if (sf::Keyboard::isKeyPressed(sf::Keyboard::Scan::D)) move.x += 1;
            if (sf::Keyboard::isKeyPressed(sf::Keyboard::Scan::W)) move.y -= 1;
            if (sf::Keyboard::isKeyPressed(sf::Keyboard::Scan::S)) move.y += 1;
            float len = std::sqrt(move.x * move.x + move.y * move.y);
            if (len > 1.f) move /= len;
            player.move(move * PLAYER_SPEED * dt);

            // Clamp player inside screen
            sf::Vector2f pos = player.getPosition();
            float rP = player.getRadius();
            pos.x = std::clamp(pos.x, rP, (float)WORLD_W - rP);
            pos.y = std::clamp(pos.y, rP, (float)WORLD_H - rP);
            player.setPosition(pos);

            sf::Vector2f aim = window.mapPixelToCoords(sf::Mouse::getPosition(window)) - player.getPosition();
            float aimLen = std::sqrt(aim.x * aim.x + aim.y * aim.y);
            if (sf::Mouse::isButtonPressed(sf::Mouse::Button::Left) && shootTimer > FIRE_DELAY) {
                if (aimLen > 0.001f) aim /= aimLen;
                bullets.emplace_back(player.getPosition(), aim);
                shootTimer = 0.f;
            }

            for (auto it = bullets.begin(); it != bullets.end();) {
                if (!it->update(dt)) it = bullets.erase(it);
                else ++it;
            }

            if (enemyTimer > enemySpawnRate) {
                enemyTimer = 0.f;
                enemies.emplace_back(5 + wave, sf::Vector2f(randf(0, WINDOW_W), randf(0, WINDOW_H)));
            }

            for (auto eit = enemies.begin(); eit != enemies.end();) {
                eit->update(player.getPosition(), dt);
                bool killed = false;
                for (auto& b : bullets)
                    if (eit->hitBy(b.s.getPosition())) {
                        killed = true;
                        score += 10;
                        shakeTimer = 0.25f;
                        shakeIntensity = 8.f;
                        break;
                    }
                if (!killed && eit->hitBy(player.getPosition())) {
                    health -= 12;
                    killed = true;
                    shakeTimer = 0.4f;
                    shakeIntensity = 12.f;
		    damageFlash = 0.4f; // trigger red border flash
                }
                if (killed) eit = enemies.erase(eit);
                else ++eit;
            }

            if (score >= nextWaveScore) {
                wave++;
                nextWaveScore += 100;
                enemySpawnRate = std::max(0.7f, enemySpawnRate - 0.12f);
            }

            if (health <= 0) {
                state = GameState::GAME_OVER;
                if (score > highScore) {
                    highScore = score;
                    saveHighScore(highScore);
                }
                continue;
            }

// Camera follow + shake
static sf::Vector2f camCenter = player.getPosition();
camCenter += (player.getPosition() - camCenter) * 5.f * dt;  // smooth follow

// apply shake if active
if (shakeTimer > 0.f) {
    float sx = randf(-1, 1) * shakeIntensity;
    float sy = randf(-1, 1) * shakeIntensity;
    camCenter += sf::Vector2f(sx, sy);
}

// clamp camera so it doesn’t show outside the world edges
camCenter.x = std::clamp(camCenter.x, WINDOW_W / 2.f, WORLD_W - WINDOW_W / 2.f);
camCenter.y = std::clamp(camCenter.y, WINDOW_H / 2.f, WORLD_H - WINDOW_H / 2.f);

view.setCenter(camCenter);
window.setView(view);


            // Update UI
            float hpRatio = std::max(0.f, (float)health / MAX_HEALTH);
            hpBar.setSize({200.f * hpRatio, 16.f});
            hpBar.setFillColor(hpRatio > 0.5f ? sf::Color::Green
                                              : hpRatio > 0.25f ? sf::Color::Yellow
                                                                : sf::Color::Red);

            scoreText.setString("Score: " + std::to_string(score));
            hsText.setString("High: " + std::to_string(highScore));
            waveText.setString("Wave: " + std::to_string(wave));

            // Draw
            window.clear();

            // --- Draw world background stars using world coordinates ---
            window.setView(window.getDefaultView()); // fix stars to screen, not moving with player
            for (auto &s : stars) window.draw(s.shape);
            window.setView(view); // restore camera view for player + enemies

            // --- Draw world objects ---
            for (auto& e : enemies) e.draw(window);
            for (auto& b : bullets) {
                for (auto& g : b.trail)
                    window.draw(g.shape, sf::RenderStates(sf::BlendAdd));
                window.draw(b.s);
            }
            window.draw(player);

            // --- Draw UI --- (switch to default view so it stays fixed on screen)
            window.setView(window.getDefaultView());  
            window.draw(hpBack);
            window.draw(hpBar);
            window.draw(scoreText);
            window.draw(hsText);
            window.draw(waveText);

        } else if (state == GameState::MENU) {
            window.clear(sf::Color(20, 20, 40));
            window.draw(title);
            window.draw(startBtn);
            window.draw(exitBtn);
            hsText.setString("High: " + std::to_string(highScore));
            window.draw(hsText);
        }

        // --- Damage flash overlay ---
        if (damageFlash > 0.f) {
            damageFlash -= dt;
            float alpha = std::min(255.f, 255.f * (damageFlash / 0.4f)); // fade out
            sf::RectangleShape flash(sf::Vector2f(WINDOW_W, WINDOW_H));

            flash.setFillColor(sf::Color(255, 0, 0, static_cast<uint8_t>(alpha * 0.3f)));
            flash.setOutlineColor(sf::Color(255, 0, 0, static_cast<uint8_t>(alpha)));

            flash.setOutlineThickness(20.f);

            window.setView(window.getDefaultView());
            window.draw(flash);
            window.setView(view);
        }


        window.display();
    }

    return 0;
}
