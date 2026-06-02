# System Information Carousel

This application needs no optional hardware. It rotates every five seconds
through system, resource, network, display, interface, and device information.
A separate one-second timer updates the local clock.

System information is the snapshot captured when the application starts. The
clock is dynamic, but values such as memory, network state, and device counts do
not change until a new Python application session starts.

Use this directory in `sender_config.json`:

```json
{
  "directory": "sample_applications/system_information_carousel"
}
```
