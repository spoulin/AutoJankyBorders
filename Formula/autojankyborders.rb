class Autojankyborders < Formula
  desc "JankyBorders fork with automatic per-window border colors"
  homepage "https://github.com/spoulin/AutoJankyBorders"
  license "GPL-3.0-only"
  head "https://github.com/spoulin/AutoJankyBorders.git", branch: "main"

  depends_on :macos
  conflicts_with "borders", because: "both install a borders executable"

  def install
    system "make"
    bin.install "bin/borders"
    man1.install "docs/borders.1"
  end

  service do
    run opt_bin/"borders"
    keep_alive true
    process_type :interactive
    log_path var/"log/autojankyborders.log"
    error_log_path var/"log/autojankyborders.log"
  end
end
