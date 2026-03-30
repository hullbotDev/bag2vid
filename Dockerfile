# Ubuntu 18.04 ros melodic base image
FROM ros:melodic-ros-core-bionic

# Install dependencies
RUN apt-get update && apt-get install -y \
    python-catkin-tools \
    python-rosdep \
    python-rosinstall \
    python-rosinstall-generator \
    python-wstool \
    build-essential \
    qtmultimedia5-dev \
    ros-melodic-cv-bridge \
    libavcodec-dev \
    libavformat-dev \
    libavutil-dev \
    libswscale-dev \
    libavcodec-extra \
    pkg-config \
    && rm -rf /var/lib/apt/lists/

# Initialise rosdep
RUN rosdep init \
    && rosdep update

# Create user
RUN useradd -ms /bin/bash user

# Create XDG_RUNTIME_DIR with correct permissions
RUN mkdir -p /tmp/runtime-user && chown user:user /tmp/runtime-user && chmod 700 /tmp/runtime-user

# Create a workspace
RUN mkdir -p /home/user/catkin_ws/src && chown -R user:user /home/user/catkin_ws

# Set the workspace
WORKDIR /home/user/catkin_ws

# Copy the source code
COPY --chown=user:user . /home/user/catkin_ws/src

# Install ROS dependencies (needs root for apt)
RUN /bin/bash -c "source /opt/ros/melodic/setup.bash && rosdep install --from-paths src --ignore-src -r -y"

# Build as user
USER user
RUN /bin/bash -c "source /opt/ros/melodic/setup.bash && catkin build"

ENV XDG_RUNTIME_DIR=/tmp/runtime-user

# Source the workspace
RUN echo "source /home/user/catkin_ws/devel/setup.bash" >> /home/user/.bashrc

# Set the entrypoint
ENTRYPOINT ["bash", "-c", "source /home/user/catkin_ws/devel/setup.bash && rosrun bag2vid bag2vid_gui"]
