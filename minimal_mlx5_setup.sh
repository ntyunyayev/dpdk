meson setup -Denable_drivers=bus/auxiliary,net/mlx5 -Ddisable_apps=* -Dprefix=$PWD/install build --wipe
