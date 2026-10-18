const video = document.querySelector('#views-video');
const toggle = document.querySelector('#video-toggle');
const chapters = document.querySelector('.view-chapters');
const reducedMotion = window.matchMedia('(prefers-reduced-motion: reduce)');
if (video && toggle && chapters) {
  video.controls = false;
  toggle.hidden = false;
  chapters.hidden = false;
  const buttons = [...chapters.querySelectorAll('button')];
  const update = () => {
    toggle.textContent = video.paused ? 'Play demo' : 'Pause demo';
    buttons.forEach((button, index) => {
      button.setAttribute('aria-current', String(Math.floor(video.currentTime / 4) === index));
    });
  };
  const play = () => video.play().catch(() => { video.controls = true; update(); });
  toggle.addEventListener('click', () => video.paused ? play() : video.pause());
  buttons.forEach(button => button.addEventListener('click', () => {
    video.currentTime = Number(button.dataset.time);
    play();
  }));
  video.addEventListener('play', update);
  video.addEventListener('pause', update);
  video.addEventListener('timeupdate', update);
  reducedMotion.addEventListener('change', () => { if (reducedMotion.matches) video.pause(); });
  update();
  if (!reducedMotion.matches) play();
}
