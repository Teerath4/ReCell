// Uses the WebUI Brick's own client library (not raw socket.io directly) —
// confirmed against Arduino's own App Lab example pattern.
const ui = new WebUI();

ui.on_connect(() => console.log('Connected to backend'));
ui.on_disconnect(() => console.log('Connection to backend lost'));

ui.on_message('cell_update', (data) => {
  const card = document.getElementById(`cell-${data.cell_id}`);
  if (!card) return;
  card.querySelector('.cell-soh').textContent = `${data.soh_percent}%`;
  card.querySelector('.cell-recommendation').textContent = data.recommendation;
});

ui.on_message('pack_update', (data) => {
  document.getElementById('pack-soh-value').textContent = `${data.pack_soh_percent}%`;
  document.getElementById('pack-recommendation-value').textContent = data.recommendation;
  document.getElementById('pack-weakest-value').textContent = `Cell ${data.weakest_cell_id}`;

  document.querySelectorAll('.cell-card').forEach((el) => el.classList.remove('weak'));
  const weakCard = document.getElementById(`cell-${data.weakest_cell_id}`);
  if (weakCard) weakCard.classList.add('weak');
});
