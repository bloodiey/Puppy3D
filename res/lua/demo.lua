-- Puppy3D Lua scene example.
-- The editor calls update(delta_time) every frame while the 3D view is open.

engine.clear_scene()

local cube = engine.add_cube("Lua cube", 4, 4, 4)
engine.set_color(cube, 0.25, 0.65, 0.95)

local angle = 0

function update(delta_time)
    angle = (angle + 60 * delta_time) % 360
    engine.set_rotation(cube, 20, angle, 0)
end
