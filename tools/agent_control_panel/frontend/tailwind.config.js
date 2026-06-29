/** @type {import('tailwindcss').Config} */
export default {
  content: ["./index.html", "./src/**/*.{vue,js}"],
  theme: {
    extend: {
      colors: {
        panel: "#0f172a",
        surface: "#111827",
        edge: "#1f2937",
        ink: "#e5eefc",
        mute: "#94a3b8",
        accent: "#22d3ee",
        accent2: "#38bdf8",
      },
      boxShadow: {
        panel: "0 20px 45px rgba(2, 8, 23, 0.35)",
      },
    },
  },
  plugins: [],
};
