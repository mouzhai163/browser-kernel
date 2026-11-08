# MZ Browser Components

This directory contains core components for the MZ browser.

## Directory Structure

```
mz/
├── auth/                               # Authentication module
├── config/                             # Configuration management
├── eventbus/                           # EventBus IPC framework
├── fingerprint/                        # Browser fingerprinting
├── proxy/                              # Proxy configuration (NEW)
│   ├── proxy_config_service.h/cc       # Core proxy service
│   ├── proxy_config_parser.h/cc        # JSON parser
│   ├── proxy_message_handler.h/cc      # EventBus handler
│   └── network_context_configurator.h/cc
├── mz_browser_context.h/cc       # Global context manager
└── BUILD.gn
```

## Modules

### EventBus
IPC communication framework between Tauri and Chromium.
- Named pipe transport
- Message routing and handling
- Topic-based pub/sub

See [eventbus/README.md](eventbus/README.md) for details.

### Proxy
Dynamic proxy configuration with runtime updates.
- HTTP, HTTPS, SOCKS5 support
- Proxy authentication
- EventBus integration

See [proxy/README.md](proxy/README.md) for details.

### Auth
Authentication and authorization.
- User authentication
- Token management
- Permission control

### Config
Configuration management.
- Launch configuration
- Runtime configuration updates

### Fingerprint
Browser fingerprinting and anti-detection.
- Canvas fingerprinting
- WebGL fingerprinting
- Audio context fingerprinting

## Global Context

`MzBrowserContext` is a singleton that manages the lifecycle of all MZ components:

```cpp
auto* context = mz::MzBrowserContext::GetInstance();

// Initialize during browser startup
context->Initialize();

// Access components
auto* proxy_service = context->proxy_config_service();
auto* proxy_handler = context->proxy_message_handler();

// Shutdown during browser exit
context->Shutdown();
```

## Integration Points

### Chrome Browser Main
- `chrome_browser_main.cc` initializes MzBrowserContext
- Integrated into browser startup/shutdown lifecycle

### EventBus
- All modules register message handlers with EventBus
- Enables IPC communication with Tauri

### Network Stack
- Proxy module integrates with Chromium's network stack
- ProxyConfigMonitor observes configuration changes

## Build Configuration

Each module has its own BUILD.gn:

```gn
# Root BUILD.gn
source_set("browser_context") {
  deps = [
    "//mz/eventbus",
    "//mz/proxy",
  ]
}

# proxy/BUILD.gn
source_set("proxy") {
  deps = [
    "//base",
    "//net",
    "//mz/eventbus",
  ]
}
```

## Development Guidelines

### Adding New Modules

1. Create a subdirectory under `mz/`
2. Add BUILD.gn with appropriate dependencies
3. Implement EventBus message handlers if needed
4. Register with MzBrowserContext if needed
5. Add README.md documenting the module

### Code Style

- Follow Chromium C++ style guide
- Use `mz` namespace
- Use `SEQUENCE_CHECKER` for thread safety
- Add comprehensive logging

### Testing

- Write unit tests for each component
- Add integration tests for cross-module functionality
- Test EventBus message handling

## License

This directory is part of the `mz-browser-kernel` repository and follows the repository-level license.
