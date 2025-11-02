# Pyglet Physics Game

A 2D physics simulation game built with **Pyglet** and **Pymunk**, featuring interactive tools and real-time physics.

## 🚀 Features

### **Physics Tools**
- **Collision Tool (C)**: Draw collision boundaries
- **Wind Tool (W)**: Create wind zones that push objects
- **Magnet Tool (M)**: Create magnetic attraction/repulsion zones
- **Teleporter Tool (T)**: Create instant teleportation between two points
- **FreeDraw Tool (F)**: Draw freehand collision boundaries

### **Global Physics Control**
- **Ctrl + Mouse Up/Down**: Control gravity (top = inverted, bottom = super heavy, center = normal)
- **Ctrl + Mouse Left/Right**: Control wind direction and strength
- **Center Snap**: Automatic snapping to normal physics when near center

### **Interactive Features**
- **Right Click**: Activate erase tool (deletes objects within 50px radius)
- **Middle Click**: Delete objects at specific position
- **Automatic Object Spawning**: Objects spawn from the top of the screen
- **Real-time Physics**: 60 FPS physics simulation

## 🛠️ Installation

1. **Install dependencies**:
   ```bash
   pip install -r requirements.txt
   ```

2. **Run the game**:
   ```bash
   python main.py
   ```

## 🎮 Controls

| Key | Action |
|-----|--------|
| **TAB** | Switch between tools |
| **SPACE** | Spawn random object |
| **C** | Clear all objects |
| **R** | Reset physics to default |
| **Mouse** | Use current tool |
| **Ctrl + Mouse** | Control global physics |
| **Right Click** | Erase tool |
| **Middle Click** | Delete at position |

## 🔧 Tool Usage

### **Collision Tool**
- Click and drag to draw collision boundaries
- Objects will bounce off these boundaries

### **Wind Tool**
- Click to create wind zones
- Wind direction is calculated from screen center
- Objects in wind zones are pushed in the wind direction

### **Magnet Tool**
- Left click to create attraction zones (green)
- Objects are pulled toward attraction zones
- Repulsion zones (red) push objects away

### **Teleporter Tool**
- Click first position to start teleporter
- Click second position to complete the pair
- Objects touching either teleporter are instantly moved to the other

### **FreeDraw Tool**
- Click and drag to draw freehand collision boundaries
- Creates multiple line segments for smooth curves

## 🌟 Key Differences from Pygame Version

### **Rendering System**
- Uses **Pyglet's built-in shapes module** instead of Pygame drawing
- **OpenGL-based rendering** for better performance
- **Batch rendering** for efficient graphics

### **Event Handling**
- **Pyglet event decorators** instead of Pygame event loop
- **Automatic window management** and event dispatching
- **Built-in mouse and keyboard handling**

### **Performance Benefits**
- **Hardware acceleration** through OpenGL
- **Efficient batching** of draw calls
- **Better memory management** for graphics objects

## 🐛 Troubleshooting

### **Common Issues**

1. **"No module named 'pyglet'"**
   - Install pyglet: `pip install pyglet`

2. **"No module named 'pymunk'"**
   - Install pymunk: `pip install pymunk`

3. **Graphics not rendering**
   - Check if your graphics drivers support OpenGL
   - Try updating graphics drivers

4. **Performance issues**
   - Reduce object spawn rate in the code
   - Close other graphics-intensive applications

### **Debug Mode**
The game includes extensive debug output. Check the console for:
- Object spawning/deletion messages
- Physics calculations
- Tool usage information
- Error messages with full tracebacks

## 🔮 Future Enhancements

### **Planned Features**
- **Particle effects** for collisions and teleportation
- **Sound effects** using Pyglet's audio capabilities
- **Save/load** for physics scenes
- **More physics tools** (springs, joints, fluids)
- **3D rendering** using Pyglet's OpenGL features

### **Performance Optimizations**
- **Spatial partitioning** for collision detection
- **Level of detail** rendering for distant objects
- **Object pooling** to reduce memory allocation

## 📚 Technical Details

### **Architecture**
- **Hybrid approach**: Pyglet rendering + Pymunk physics
- **Event-driven design** with Pyglet decorators
- **Batch rendering** for efficient graphics
- **Modular tool system** for easy extension

### **Physics Engine**
- **Pymunk 2D physics** for realistic simulation
- **60 FPS physics stepping** for smooth motion
- **Collision detection** and response
- **Force-based interactions** (wind, magnets, gravity)

### **Rendering Pipeline**
- **Pyglet shapes module** for basic shapes
- **Batch rendering** for performance
- **Opacity and color management**
- **Real-time preview rendering**

## 🤝 Contributing

Feel free to contribute by:
- Adding new physics tools
- Improving performance
- Adding visual effects
- Fixing bugs
- Enhancing documentation

## 📄 License

This project is open source and available under the MIT License.
