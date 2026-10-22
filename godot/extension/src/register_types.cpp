// Registers the Overgrowth classes with Godot: the plant generator, the demo world and the instancing layer.
#include <gdextension_interface.h>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>

#include "demo_world.h"
#include "plant_generator.h"
#include "plant_layer.h"

using namespace godot;

static void initialize_overgrowth(ModuleInitializationLevel level) {
    if (level != MODULE_INITIALIZATION_LEVEL_SCENE) return;
    GDREGISTER_CLASS(PlantGenerator);
    GDREGISTER_CLASS(DemoWorld);
    GDREGISTER_CLASS(PlantLayer);
}

static void uninitialize_overgrowth(ModuleInitializationLevel level) {}

extern "C" {
GDExtensionBool GDE_EXPORT overgrowth_library_init(GDExtensionInterfaceGetProcAddress get_proc_address,
                                                   GDExtensionClassLibraryPtr library,
                                                   GDExtensionInitialization* initialization) {
    GDExtensionBinding::InitObject init(get_proc_address, library, initialization);
    init.register_initializer(initialize_overgrowth);
    init.register_terminator(uninitialize_overgrowth);
    init.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);
    return init.init();
}
}
