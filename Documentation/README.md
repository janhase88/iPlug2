# Documentation

This is the home of documentation for iPlug2, which is a work in progress. The [iPlug 2 Wiki](https://github.com/iPlug2/iPlug2/wiki) includes some more high-level information, that may move here eventually.

If you're viewing this as a markdown file on GitHub, you can see the doxygen version [here](https://iplug2.github.io/docs/).

In the future, this will include some tutorials and guides, but for now it's just the doxygen docs for the classes. Most classes and files have been entered, but many methods are not yet documented, or are documented with out of date information.

In the meantime, you can read two academic papers that we wrote about the project, which are in the documentation folder, and you can watch the accompanying presentations on YouTube:

1 - International Faust Conference (IFC 2018) [PAPER](https://github.com/iPlug2/iPlug2/raw/master/Documentation/Papers/IFC2018.pdf) | [VIDEO](https://youtu.be/SLHGxBYeID4)

2 - Web Audio Conference (WAC2018) [PAPER](https://github.com/iPlug2/iPlug2/raw/master/Documentation/Papers/WAC2018.pdf) | [VIDEO](https://youtu.be/DDrgW4Qyz8Y)

## Debugging multiple instances

When building with tracing enabled, each log line now begins with an `inst=` field. The value is the `mInstanceID` pointer from `IPlugAPIBase`, providing a unique identifier for every plug-in instance. Matching `inst` values across messages lets you follow activity from a specific instance while running several copies of a plug-in.

<!--
## Introduction

### Requirements
iPlug2 requires a compiler that supports C++14, and is tested with MS Visual Studio 2019 and Xcode 11. It is developed to target Windows 8 or higher and macOS 10.9+. If you wish to compile for older operating systems it may be possible, but will require adjusting some settings.

## About this documentation
### Where do I begin?
See [Getting Started](md_quickstart.html) and check out the [Examples](md_examples.html)
-->
