{
  description = "HashWeb UI - QML editor + C++ backend module over hashweb_module";

  inputs = {
    # Must be the same builder hashweb_module consumes, so the
    # logos-protocol/logos-qt-sdk chain matches across both.
    logos-module-builder.url = "github:logos-co/logos-module-builder";
    # The hub module this UI is a thin client of.
    hashweb_module.url = "github:davidrusu/hashweb-module";
    # Kept in lockstep with hashweb_module's delivery pin (v0.1.3).
    logos-delivery-module.url = "github:logos-co/logos-delivery-module/v0.1.3";
  };

  outputs = inputs@{ logos-module-builder, logos-delivery-module, ... }:
    logos-module-builder.lib.mkLogosQmlModule {
      src = ./.;
      configFile = ./metadata.json;
      flakeInputs = { delivery_module = logos-delivery-module; } // inputs;
    };
}
