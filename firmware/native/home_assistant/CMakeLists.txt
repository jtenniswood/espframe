idf_component_register(SRCS esphome_session.cpp esphome_socket.cpp INCLUDE_DIRS "."
                       PRIV_INCLUDE_DIRS "../main" "../../../components/espframe"
                       PRIV_REQUIRES esphome__noise-c lwip)
