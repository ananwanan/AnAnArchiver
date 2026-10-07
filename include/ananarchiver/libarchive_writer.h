#pragma once

#include "ananarchiver/archive_writer.h"

#include <memory>

namespace ananarchiver {

std::unique_ptr<ArchiveWriter> makeLibArchiveWriter();

} // namespace ananarchiver
