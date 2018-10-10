/*
 *  EyelinkPlugin.cpp
 *  Eyelink
 *
 *  Created by Philipp Schwedhelm on 13.12.10.
 *  Copyright 2010 German Primate Center. All rights reserved.
 *
 */

#include "Eyelink.h"


BEGIN_NAMESPACE_MW


class EyelinkPlugin : public Plugin {
    void registerComponents(boost::shared_ptr<ComponentRegistry> registry) override {
        registry->registerFactory<StandardComponentFactory, Eyelink>();
    }
};


extern "C" Plugin* getPlugin() {
    return new EyelinkPlugin();
}


END_NAMESPACE_MW
